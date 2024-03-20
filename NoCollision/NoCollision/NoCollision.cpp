#include <API/ARK/Ark.h>

#include "NoCollisionSqlLite.h"

#pragma comment(lib, "ArkApi.lib")

struct UActorChannel;

DECLARE_HOOK(APrimalStructure_IsAllowedToBuild, int, APrimalStructure*, APlayerController*, FVector, FRotator,
	FPlacementData*, bool, FRotator, bool);
DECLARE_HOOK(UActorChannel_ReplicateActor, bool, UActorChannel *);

struct UNetConnection
{
	TSharedPtr<FUniqueNetId, 0>& PlayerIdField()
	{
		return *GetNativePointerField<TSharedPtr<FUniqueNetId, 0>*>(this, "UNetConnection.PlayerId");
	}
};

struct UChannel
{
	UNetConnection* ConnectionField() { return *GetNativePointerField<UNetConnection **>(this, "UChannel.Connection"); }
};

struct UActorChannel : UChannel
{
};

int Hook_APrimalStructure_IsAllowedToBuild(APrimalStructure* _this, APlayerController* PC, FVector AtLocation,
                                           FRotator AtRotation, FPlacementData* OutPlacementData,
                                           bool bDontAdjustForMaxRange, FRotator PlayerViewRotation,
                                           bool bFinalPlacement)
{
	const auto res = APrimalStructure_IsAllowedToBuild_original(_this, PC, AtLocation, AtRotation, OutPlacementData,
	                                                            bDontAdjustForMaxRange, PlayerViewRotation,
	                                                            bFinalPlacement);

	if (PC && PC->IsA(AShooterPlayerController::GetPrivateStaticClass()))
	{
		AShooterPlayerController* aspc = static_cast<AShooterPlayerController*>(PC);

		const auto steam_id = ArkApi::GetApiUtils().GetSteamIdFromController(aspc);

		return AllowedNoCollisionBuilders.Contains(steam_id)
			       ? 211
			       : res;
	}

	return res;
}

bool Hook_UActorChannel_ReplicateActor(UActorChannel* _this)
{
	if (_this->ConnectionField())
	{
		FUniqueNetIdSteam* steam_net_id = static_cast<FUniqueNetIdSteam*>(_this->ConnectionField()
		                                                                       ->PlayerIdField().Get());
		const uint64 steam_id = steam_net_id->UniqueNetId;
		if (AllowedNoCollisionBuilders.Contains(steam_id))
		{
			AShooterGameMode* game_mode = ArkApi::GetApiUtils().GetShooterGameMode();

			game_mode->bDisableStructurePlacementCollisionField() = true;
			static_cast<AShooterGameState*>(game_mode->GameStateField())
				->bDisableStructurePlacementCollisionField() = true;

			const auto res = UActorChannel_ReplicateActor_original(_this);

			game_mode->bDisableStructurePlacementCollisionField() = false;
			static_cast<AShooterGameState*>(game_mode->GameStateField())
				->bDisableStructurePlacementCollisionField() = false;

			return res;
		}
	}

	return UActorChannel_ReplicateActor_original(_this);
}

void AddNoCollision(APlayerController* player_controller, FString* message, bool)
{
	AShooterPlayerController* player = static_cast<AShooterPlayerController*>(player_controller);
	if (!player || !player->PlayerStateField() || !player->GetPlayerCharacter())
		return;

	TArray<FString> parsed;
	message->ParseIntoArray(parsed, L" ", true);

	if (parsed.IsValidIndex(1))
	{
		try
		{
			uint64 steam_id = std::stoull(parsed[1].ToString().c_str());

			if (!AllowedNoCollisionBuilders.Contains(steam_id))
			{
				AllowedNoCollisionBuilders.Add(steam_id);
				DBAddNoCollision(steam_id);

				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(0, 1, 0), L"{} Added to NoCollision", steam_id);
			}
			else
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0, 0, 0), L"{} Already exists in NoCollision",
				                                        steam_id);
			}
		}
		catch (...)
		{
			ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0, 0, 0),
			                                        L"Incorrect Syntax: cheat nc.add <SteamID>");
		}
	}
	else
	{
		ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0, 0, 0),
		                                        L"Incorrect Syntax: cheat nc.add <SteamID>");
	}
}

void RemoveNoCollision(APlayerController* player_controller, FString* message, bool)
{
	AShooterPlayerController* player = static_cast<AShooterPlayerController*>(player_controller);
	if (!player || !player->PlayerStateField() || !player->GetPlayerCharacter())
		return;

	TArray<FString> parsed;
	message->ParseIntoArray(parsed, L" ", true);

	if (parsed.IsValidIndex(1))
	{
		try
		{
			uint64 steam_id = std::stoull(parsed[1].ToString().c_str());

			if (AllowedNoCollisionBuilders.Contains(steam_id))
			{
				AllowedNoCollisionBuilders.Remove(steam_id);
				DBRemoveNoCollision(steam_id);

				AShooterPlayerController* target_player = ArkApi::GetApiUtils().FindPlayerFromSteamId(steam_id);
				if (target_player && target_player->bIsAdmin()())
				{
					FString kick_reason = L"No Collision Removed!";
					ArkApi::GetApiUtils().GetShooterGameMode()->KickPlayerController(target_player, &kick_reason);
				}

				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(0, 1, 0), L"{} Removed from NoCollision", steam_id);
			}
			else
			{
				ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0, 0, 0), L"{} Does not exists in NoCollision",
				                                        steam_id);
			}
		}
		catch (...)
		{
			ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0, 0, 0),
			                                        L"Incorrect Syntax: cheat nc.remove <SteamID>");
		}
	}
	else
	{
		ArkApi::GetApiUtils().SendServerMessage(player, FLinearColor(1.0, 0, 0),
		                                        L"Incorrect Syntax: cheat nc.remove <SteamID>");
	}
}

void Load()
{
	Log::Get().Init("NoCollision");

	InitSqlLite();

	ArkApi::GetHooks().SetHook("APrimalStructure.IsAllowedToBuild", &Hook_APrimalStructure_IsAllowedToBuild,
	                           &APrimalStructure_IsAllowedToBuild_original);
	ArkApi::GetHooks().SetHook("UActorChannel.ReplicateActor", &Hook_UActorChannel_ReplicateActor,
	                           &UActorChannel_ReplicateActor_original);

	ArkApi::GetCommands().AddConsoleCommand("nc.add", &AddNoCollision);
	ArkApi::GetCommands().AddConsoleCommand("nc.remove", &RemoveNoCollision);
}

void UnLoad()
{
	ArkApi::GetHooks().DisableHook("APrimalStructure.IsAllowedToBuild", &Hook_APrimalStructure_IsAllowedToBuild);
	ArkApi::GetHooks().DisableHook("UActorChannel.ReplicateActor", &Hook_UActorChannel_ReplicateActor);

	ArkApi::GetCommands().RemoveConsoleCommand("nc.add");
	ArkApi::GetCommands().RemoveConsoleCommand("nc.remove");
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		Load();
		break;
	case DLL_PROCESS_DETACH:
		UnLoad();
		break;
	}
	return TRUE;
}
