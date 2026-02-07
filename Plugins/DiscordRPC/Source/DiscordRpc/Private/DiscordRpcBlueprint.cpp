#include "DiscordRpcBlueprint.h"
#include "DiscordRpcPrivatePCH.h"
#include "DiscordRpcBlueprint.h"
#include <string>
#include "discord_rpc.h"

DEFINE_LOG_CATEGORY(Discord)

static UDiscordRpc* self = nullptr;

static void ReadyHandler(const DiscordUser* connectedUser)
{
    FDiscordUserData ud;
    ud.userId = ANSI_TO_TCHAR(connectedUser->userId);
    ud.username = ANSI_TO_TCHAR(connectedUser->username);
    ud.discriminator = ANSI_TO_TCHAR(connectedUser->discriminator);
    ud.avatar = ANSI_TO_TCHAR(connectedUser->avatar);
    UE_LOG(Discord,
           Log,
           TEXT("Discord connected to %s - %s#%s"),
           *ud.userId,
           *ud.username,
           *ud.discriminator);
    if (self) {
        self->IsConnected = true;
        self->OnConnected.Broadcast(ud);
    }
}

static void DisconnectHandler(int errorCode, const char* message)
{
    auto msg = FString(message);
    UE_LOG(Discord, Log, TEXT("Discord disconnected (%d): %s"), errorCode, *msg);
    if (self) {
        self->IsConnected = false;
        self->OnDisconnected.Broadcast(errorCode, msg);
    }
}

static void ErroredHandler(int errorCode, const char* message)
{
    auto msg = FString(message);
    UE_LOG(Discord, Log, TEXT("Discord error (%d): %s"), errorCode, *msg);
    if (self) {
        self->OnErrored.Broadcast(errorCode, msg);
    }
}

static void JoinGameHandler(const char* joinSecret)
{
    auto secret = FString(joinSecret);
    UE_LOG(Discord, Log, TEXT("Discord join %s"), *secret);
    if (self) {
        self->OnJoin.Broadcast(secret);
    }
}

static void SpectateGameHandler(const char* spectateSecret)
{
    auto secret = FString(spectateSecret);
    UE_LOG(Discord, Log, TEXT("Discord spectate %s"), *secret);
    if (self) {
        self->OnSpectate.Broadcast(secret);
    }
}

static void JoinRequestHandler(const DiscordUser* request)
{
    FDiscordUserData ud;
    ud.userId = ANSI_TO_TCHAR(request->userId);
    ud.username = ANSI_TO_TCHAR(request->username);
    ud.discriminator = ANSI_TO_TCHAR(request->discriminator);
    ud.avatar = ANSI_TO_TCHAR(request->avatar);
    UE_LOG(Discord,
           Log,
           TEXT("Discord join request from %s - %s#%s"),
           *ud.userId,
           *ud.username,
           *ud.discriminator);
    if (self) {
        self->OnJoinRequest.Broadcast(ud);
    }
}

void UDiscordRpc::Initialize(const FString& applicationId,
                             bool autoRegister,
                             const FString& optionalSteamId)
{
    self = this;
    IsConnected = false;
    DiscordEventHandlers handlers{};
    handlers.ready = ReadyHandler;
    handlers.disconnected = DisconnectHandler;
    handlers.errored = ErroredHandler;
    if (OnJoin.IsBound()) {
        handlers.joinGame = JoinGameHandler;
    }
    if (OnSpectate.IsBound()) {
        handlers.spectateGame = SpectateGameHandler;
    }
    if (OnJoinRequest.IsBound()) {
        handlers.joinRequest = JoinRequestHandler;
    }
    auto appId = StringCast<ANSICHAR>(*applicationId);
    auto steamId = StringCast<ANSICHAR>(*optionalSteamId);
    Discord_Initialize(
      (const char*)appId.Get(), &handlers, autoRegister, (const char*)steamId.Get());
}

void UDiscordRpc::Shutdown()
{
    Discord_Shutdown();
    self = nullptr;
}

void UDiscordRpc::RunCallbacks()
{
    Discord_RunCallbacks();
}

void UDiscordRpc::UpdatePresence()
{
    DiscordRichPresence rp{};

    FString state = *RichPresence.state;
    FString details = *RichPresence.details;
    FString largeImageKey = *RichPresence.largeImageKey;
    FString largeImageText = *RichPresence.largeImageText;
    FString smallImageKey = *RichPresence.smallImageKey;
    FString smallImageText = *RichPresence.smallImageText;
    FString partyId = *RichPresence.partyId;
    FString matchSecret = *RichPresence.matchSecret;
    FString joinSecret = *RichPresence.joinSecret;
    FString spectateSecret = *RichPresence.spectateSecret;

    std::string stateUtf8(TCHAR_TO_UTF8(*state));
    std::string detailsUtf8(TCHAR_TO_UTF8(*details));
    std::string largeImageKeyUtf8(TCHAR_TO_UTF8(*largeImageKey));
    std::string largeImageTextUtf8(TCHAR_TO_UTF8(*largeImageText));
    std::string smallImageKeyUtf8(TCHAR_TO_UTF8(*smallImageKey));
    std::string smallImageTextUtf8(TCHAR_TO_UTF8(*smallImageText));
    std::string partyIdUtf8(TCHAR_TO_UTF8(*partyId));
    std::string matchSecretUtf8(TCHAR_TO_UTF8(*matchSecret));
    std::string joinSecretUtf8(TCHAR_TO_UTF8(*joinSecret));
    std::string spectateSecretUtf8(TCHAR_TO_UTF8(*spectateSecret));

    rp.state = stateUtf8.c_str();
    rp.details = detailsUtf8.c_str();
    rp.largeImageKey = largeImageKeyUtf8.c_str();
    rp.largeImageText = largeImageTextUtf8.c_str();
    rp.smallImageKey = smallImageKeyUtf8.c_str();
    rp.smallImageText = smallImageTextUtf8.c_str();
    rp.partyId = partyIdUtf8.c_str();
    rp.matchSecret = matchSecretUtf8.c_str();
    rp.joinSecret = joinSecretUtf8.c_str();
    rp.spectateSecret = spectateSecretUtf8.c_str();

    rp.startTimestamp = RichPresence.startTimestamp;
    rp.endTimestamp = RichPresence.endTimestamp;
    rp.partySize = RichPresence.partySize;
    rp.partyMax = RichPresence.partyMax;
    rp.instance = RichPresence.instance;

    Discord_UpdatePresence(&rp);
}

void UDiscordRpc::ClearPresence()
{
    Discord_ClearPresence();
}

void UDiscordRpc::Respond(const FString& userId, int reply)
{
    UE_LOG(Discord, Log, TEXT("Responding %d to join request from %s"), reply, *userId);
    FTCHARToUTF8 utf8_userid(*userId);
    Discord_Respond(utf8_userid.Get(), reply);
}
