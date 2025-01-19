#include <stdio.h>
#include "faceit_level.h"
#include "metamod_oslink.h"
#include <nlohmann/json.hpp>
#include "schemasystem/schemasystem.h"
#include <steam/steam_gameserver.h>
#include <condition_variable>
#include <thread>
Faceit_Level g_Faceit_Level;
PLUGIN_EXPOSE(Faceit_Level, g_Faceit_Level);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

CSteamGameServerAPIContext g_steamAPI;
ISteamHTTP *g_http = nullptr;

IPlayersApi* g_pPlayers;
IUtilsApi* g_pUtils;
ISkinChangerApi* g_pSkinChanger;

using json = nlohmann::json;

const char* g_szFaceitKey;
bool g_bUseCSGO;

bool g_bFaceitLevel[64];
int g_iFaceitLevel[64];

bool g_bDefaultStatus;

std::map<int, int> g_iLevels;

KeyValues* g_hKVData;
std::map<std::string, std::string> g_vecPhrases;

SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);

class PlayerFaceitFetcher
{
public:
    PlayerFaceitFetcher(uint64 steamID) : m_steamID(steamID), m_faceitReady(false) {}

    int GetPlayerFaceit(bool bCSGO = false)
    {
        m_faceitReady = false;
        char szURL[512];
        g_SMAPI->Format(szURL, sizeof(szURL), "https://open.faceit.com/data/v4/players?game=%s&game_player_id=%llu", bCSGO?"csgo":"cs2", m_steamID);
        auto hReq = g_http->CreateHTTPRequest(k_EHTTPMethodGET, szURL);
		char szHeader[128];
		g_SMAPI->Format(szHeader, sizeof(szHeader), "Bearer %s", g_szFaceitKey);
		g_http->SetHTTPRequestHeaderValue(hReq, "Authorization", szHeader);
		g_http->SetHTTPRequestHeaderValue(hReq, "Accept", "application/json");
        SteamAPICall_t hCall;
        g_http->SendHTTPRequest(hReq, &hCall);
        m_httpRequestCallback.SetGameserverFlag();
        m_httpRequestCallback.Set(hCall, this, &PlayerFaceitFetcher::OnGetPlayerFaceit);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_faceitReady; });

        return m_faceitLevel;
    }

private:
    void OnGetPlayerFaceit(HTTPRequestCompleted_t* pResult, bool bFailed)
    {
        if (bFailed || pResult->m_eStatusCode < 200 || pResult->m_eStatusCode > 299)
        {
			m_faceitLevel = 0;
        }
        else
        {
            uint32 size;
            g_http->GetHTTPResponseBodySize(pResult->m_hRequest, &size);
            uint8* response = new uint8[size + 1];
            g_http->GetHTTPResponseBodyData(pResult->m_hRequest, response, size);
            response[size] = 0;

            json jsonResponse;
            if (size > 0)
            {
                jsonResponse = json::parse((char*)response, nullptr, false);
                if (jsonResponse.is_discarded())
                {
                    Msg("Failed parsing JSON from HTTP response: %s\n", (char*)response);
                }
                else
                {
                    if (jsonResponse.contains("games") && !jsonResponse["games"].is_null())
                    {
						if(jsonResponse["games"].contains("cs2") && !jsonResponse["games"]["cs2"].is_null() && jsonResponse["games"]["cs2"].contains("skill_level") && !jsonResponse["games"]["cs2"]["skill_level"].is_null())
						{
							m_faceitLevel = jsonResponse["games"]["cs2"]["skill_level"].get<int>();
						}
						else if(jsonResponse["games"].contains("csgo") && !jsonResponse["games"]["csgo"].is_null() && jsonResponse["games"]["csgo"].contains("skill_level") && !jsonResponse["games"]["csgo"]["skill_level"].is_null())
						{
							m_faceitLevel = jsonResponse["games"]["csgo"]["skill_level"].get<int>();
						}
						else 
							m_faceitLevel = 0;
                    }
                }
            }
            delete[] response;
        }
        if (g_http)
            g_http->ReleaseHTTPRequest(pResult->m_hRequest);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_faceitReady = true;
        }
        m_cv.notify_one();
    }

    uint64 m_steamID;
	int m_faceitLevel;
    bool m_faceitReady;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    CCallResult<PlayerFaceitFetcher, HTTPRequestCompleted_t> m_httpRequestCallback;
};

int GetPlayerFaceit(uint64 iSteamID)
{
    PlayerFaceitFetcher fetcher(iSteamID);
	int iFaceitLevel = fetcher.GetPlayerFaceit();
	if(iFaceitLevel <= 0 && g_bUseCSGO)
	{
		iFaceitLevel = fetcher.GetPlayerFaceit(true);
	}
	return iFaceitLevel;
}

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool GetClientFaceitStatus(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return g_bDefaultStatus;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return g_bDefaultStatus;
	return g_hKVData->GetBool(std::to_string(m_steamID).c_str(), g_bDefaultStatus);
}

void SaveClientFaceitStatus(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return;
	g_hKVData->SetBool(std::to_string(m_steamID).c_str(), g_bFaceitLevel[iSlot]);
	g_hKVData->SaveToFile(g_pFullFileSystem, "addons/data/faceit_data.ini");
}

std::vector<std::string> split(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

void LoadConfig()
{
	KeyValues* hKv = new KeyValues("Faceit");
	const char *pszPath = "addons/configs/faceit_settings.ini";

	if (!hKv->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		g_pUtils->ErrorLog("[FACEIT LEVEL] Failed to load %s", pszPath);
		return;
	}

	g_bDefaultStatus = hKv->GetBool("default_status", true);
	g_szFaceitKey = hKv->GetString("faceit_key", "");
	g_bUseCSGO = hKv->GetBool("use_csgo", false);
	const char* szCommand = hKv->GetString("command", "!faceit");
	std::vector<std::string> vecCommands = split(szCommand, ",");
	g_pUtils->RegCommand(g_PLID, {}, vecCommands, [](int iSlot, const char* szContent)
	{
		if(g_bFaceitLevel[iSlot])
		{
			g_bFaceitLevel[iSlot] = false;
			SaveClientFaceitStatus(iSlot);
			g_pUtils->PrintToChat(iSlot, g_vecPhrases["Faceit_Disable"].c_str());

			CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(iSlot);
			if (!pPlayerController || pPlayerController->m_steamID() == 0) return false;
			CBasePlayerPawn* pPlayer = pPlayerController->m_hPawn();
			if (!pPlayer || !pPlayerController->m_hPlayerPawn()) return false;
			pPlayerController->m_pInventoryServices()->m_rank()[5] = 0;
			g_pUtils->SetStateChanged(pPlayerController, "CCSPlayerController", "m_pInventoryServices");
		}
		else
		{
			g_bFaceitLevel[iSlot] = true;
			SaveClientFaceitStatus(iSlot);
			g_pUtils->PrintToChat(iSlot, g_vecPhrases["Faceit_Enable"].c_str());
		}
		return false;
	});
}

void LoadTranslations()
{
	KeyValues::AutoDelete g_kvPhrases("Phrases");
	const char *pszPath = "addons/translations/faceit.phrases.txt";
	if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return;
	}

	std::string szLanguage = std::string(g_pUtils->GetLanguage());
	const char* g_pszLanguage = szLanguage.c_str();
	for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
		g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
}

bool LoadData()
{
	g_hKVData = new KeyValues("Data");

	const char *pszPath = "addons/data/faceit_data.ini";

	if (!g_hKVData->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		g_pUtils->ErrorLog("[%s] Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
		return false;
	}

	return true;
}

void Faceit_Level::OnGameServerSteamAPIActivated()
{
	g_steamAPI.Init();
	g_http = g_steamAPI.SteamHTTP();

	RETURN_META(MRES_IGNORED);
}

bool Faceit_Level::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	
	SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &Faceit_Level::OnGameServerSteamAPIActivated), false);

	g_SMAPI->AddListener( this, this );

	g_iLevels[1] = 1088;
	g_iLevels[2] = 1087;
	g_iLevels[3] = 1032;
	g_iLevels[4] = 1055;
	g_iLevels[5] = 1041;
	g_iLevels[6] = 1074;
	g_iLevels[7] = 1039;
	g_iLevels[8] = 1067;
	g_iLevels[9] = 1061;
	g_iLevels[10] = 1017;

	return true;
}

bool Faceit_Level::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, this, &Faceit_Level::OnGameServerSteamAPIActivated, false);

	ConVar_Unregister();
	
	return true;
}

void Faceit_Level::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Players system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pSkinChanger = (ISkinChangerApi *)g_SMAPI->MetaFactory(SkinChanger_INTERFACE, &ret, NULL);
	if (ret != META_IFACE_FAILED)
	{
		g_pSkinChanger->OnSetCoin(g_PLID, [](int iSlot, int iTeam, int& iCoin)
		{
			if(g_iFaceitLevel[iSlot] > 0 && g_bFaceitLevel[iSlot]) return false;
			return true;
		});
	}
	LoadConfig();
	LoadTranslations();
	LoadData();
	g_pUtils->StartupServer(g_PLID, StartupServer);
	g_pPlayers->HookOnClientAuthorized(g_PLID, [](int iSlot, uint64 iSteamID64)
	{
		g_bFaceitLevel[iSlot] = GetClientFaceitStatus(iSlot);
		std::thread([iSlot, iSteamID64]()
		{
			g_iFaceitLevel[iSlot] = GetPlayerFaceit(iSteamID64);
		}).detach();
	});

	g_pUtils->CreateTimer(0.0f, []()
	{
		for (int i = 0; i < 64; i++)
		{
			if(g_iFaceitLevel[i] > 0 && g_bFaceitLevel[i])
			{
				CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(i);
				if (!pPlayerController || pPlayerController->m_steamID() == 0) continue;
				CBasePlayerPawn* pPlayer = pPlayerController->m_hPawn();
				if (!pPlayer || !pPlayerController->m_hPlayerPawn()) continue;

				if(pPlayerController->m_pInventoryServices()->m_rank()[5] != g_iLevels[g_iFaceitLevel[i]])
				{
					pPlayerController->m_pInventoryServices()->m_rank()[5] = g_iLevels[g_iFaceitLevel[i]];
					g_pUtils->SetStateChanged(pPlayerController, "CCSPlayerController", "m_pInventoryServices");
				}
			}
		}
		return 0.0f;
	});
}

///////////////////////////////////////
const char* Faceit_Level::GetLicense()
{
	return "GPL";
}

const char* Faceit_Level::GetVersion()
{
	return "1.1";
}

const char* Faceit_Level::GetDate()
{
	return __DATE__;
}

const char *Faceit_Level::GetLogTag()
{
	return "Faceit_Level";
}

const char* Faceit_Level::GetAuthor()
{
	return "Pisex";
}

const char* Faceit_Level::GetDescription()
{
	return "Faceit_Level";
}

const char* Faceit_Level::GetName()
{
	return "Faceit Level";
}

const char* Faceit_Level::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}
