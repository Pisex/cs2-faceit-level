#pragma once

#include <functional>
#include <string>

#define SkinChanger_INTERFACE "ISkinChangerApi"

// Callbacks
// return false to block the change
// return true to allow the change
typedef std::function<bool(int iSlot, int iTeam, int& iAgent)> SetAgentCallback;
typedef std::function<bool(int iSlot, int iTeam, int& iCoin)> SetCoinCallback;
typedef std::function<bool(int iSlot, int iTeam, int& iMusicKit)> SetMusicKitCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, int iStickerSlot, int& iSticker)> SetStickerCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, int& iStattrack)> SetStattrackCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, const char* szTag)> SetTagCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, int& iSeed)> SetSeedCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, float& fWear)> SetWearCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, int& iPaint)> SetPaintCallback;
typedef std::function<bool(int iSlot, int iTeam, const char* szWeapon, int& iIndex, float& fX, float& fY, float& fZ)> SetKeychainCallback;

class ISkinChangerApi
{
public:
    virtual void OnSetAgent(SourceMM::PluginId id, SetAgentCallback callback) = 0;
    virtual void OnSetCoin(SourceMM::PluginId id, SetCoinCallback callback) = 0;
    virtual void OnSetMusicKit(SourceMM::PluginId id, SetMusicKitCallback callback) = 0;
    virtual void OnSetSticker(SourceMM::PluginId id, SetStickerCallback callback) = 0;
    virtual void OnSetStattrack(SourceMM::PluginId id, SetStattrackCallback callback) = 0;
    virtual void OnSetTag(SourceMM::PluginId id, SetTagCallback callback) = 0;
    virtual void OnSetSeed(SourceMM::PluginId id, SetSeedCallback callback) = 0;
    virtual void OnSetWear(SourceMM::PluginId id, SetWearCallback callback) = 0;
    virtual void OnSetPaint(SourceMM::PluginId id, SetPaintCallback callback) = 0;
    virtual void ForceReloadSkins(int iSlot) = 0;
    virtual void OnSetKeychain(SourceMM::PluginId id, SetKeychainCallback callback) = 0;
};