# Server-Authoritative Saving System for UE-Native Authentication

Since your authentication logic runs natively within the Unreal Engine C++ multiplayer codebase (with users logging in via a UE UMG widget), the **Server-Direct REST API** approach is the most secure and robust architecture. 

Because the UE server validates and holds the active user's identity, the browser client does not need to handle database connections, session tokens, or transaction payloads. This completely mitigates browser-side security vulnerabilities (like token spoofing or JSON manipulation in the browser console).

---

## 1. Architectural Flow

```
AUTHENTICATION & INITIALIZATION:
[Player UI] -> Inputs credentials -> Server verifies -> Stores UserID in PlayerState -> Server requests user's save data -> API returns JSON -> Server applies config to ShowroomBooth -> Replicated automatically to client viewport

SAVE PIPELINE:
[Player UI] -> Click Save -> Server RPC -> Gather Booth States -> Direct HTTP POST to API (Payload: UserID + JSON config) -> API saves to DB
```

---

## 2. Where the Identity is Stored
The authenticated user identity must be stored in a **replicated, server-authoritative class** that persists throughout the session.

1. **`APlayerState` (Recommended)**: The standard Unreal class for holding player data. `APlayerState` exists on both the server and all clients, and persists if the player travels to another map.
2. **`APlayerController`**: The active controller representing the player connection on the server.

Once the C++ auth service returns a successful login, write the authenticated `UserID` (and optional auth token) to a replicated string property in your PlayerState or PlayerController:

```cpp
// In your PlayerState header (e.g. AMaxiMallPlayerState.h)
UPROPERTY(Replicated, BlueprintReadOnly, Category = "Auth")
FString AuthenticatedUserID;
```

---

## 3. Server-Direct Saving/Loading in C++

Since the server owns the identity, we use Unreal's built-in `HTTP` module to communicate directly with your backend database API.

### A. Add HTTP Dependencies
Add `"HTTP"` and `"Json"` to the public/private dependency module names in your project's `.Build.cs` file:
```csharp
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HTTP", "Json", "JsonUtilities" });
```

---

### B. The C++ Save Manager Class (`UFurnitureSaveManager`)

Implement the save manager as a World Subsystem that coordinates gathering configurations and sending async HTTP requests.

#### Header (`FurnitureSaveManager.h`):
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Http.h"
#include "FurnitureSaveManager.generated.h"

UCLASS()
class AWSTUTORIAL_API UFurnitureSaveManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Sends an async HTTP POST request to save the current world state for a user. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Save")
    void SaveWorldStateForUser(const FString& UserID, const FString& SaveName);

    /** Sends an async HTTP GET request to load the saved state for a user. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Save")
    void LoadWorldStateForUser(const FString& UserID);

private:
    FString BackendAPIUrl;

    // HTTP Callback handlers
    void OnSaveResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnLoadResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    
    // Serializes the states of all showroom booths in the world into a JSON string
    FString GatherWorldStateAsJson();
    
    // Applies the loaded JSON config to the showroom booths
    void ApplyWorldStateFromJson(const FString& JsonData);
};
```

#### Source (`FurnitureSaveManager.cpp`):
```cpp
#include "FurnitureSaveManager.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "JsonObjectConverter.h"
#include "EngineUtils.h"

void UFurnitureSaveManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // URL of your central orchestrator backend (maximall-web REST API)
    BackendAPIUrl = TEXT("https://api.maxi-mall.com/api/configurations");
}

FString UFurnitureSaveManager::GatherWorldStateAsJson()
{
    TArray<TSharedPtr<FJsonValue>> BoothsJsonArray;

    for (TActorIterator<AShowroomBooth> It(GetWorld()); It; ++It)
    {
        AShowroomBooth* Booth = *It;
        if (Booth)
        {
            TSharedPtr<FJsonObject> BoothObj = MakeShareable(new FJsonObject());
            BoothObj->SetStringField(TEXT("BoothID"), Booth->GetName());
            
            // Convert FShowroomBoothConfigState struct to JSON
            TSharedPtr<FJsonObject> StateObj = FJsonObjectConverter::UStructToJsonObject(Booth->ActiveState);
            BoothObj->SetObjectField(TEXT("ConfigState"), StateObj);

            BoothsJsonArray.Add(MakeShareable(new FJsonValueObject(BoothObj)));
        }
    }

    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
    RootObject->SetArrayField(TEXT("Booths"), BoothsJsonArray);

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    return JsonStr;
}

void UFurnitureSaveManager::SaveWorldStateForUser(const FString& UserID, const FString& SaveName)
{
    if (UserID.IsEmpty()) return;

    FString ConfigJson = GatherWorldStateAsJson();

    // Create the HTTP Request
    FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
    Request->OnProcessRequestComplete().BindUObject(this, &UFurnitureSaveManager::OnSaveResponseReceived);
    Request->SetURL(FString::Printf(TEXT("%s/save"), *BackendAPIUrl));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    // Build the request body payload
    TSharedPtr<FJsonObject> PayloadObj = MakeShareable(new FJsonObject());
    PayloadObj->SetStringField(TEXT("userId"), UserID);
    PayloadObj->SetStringField(TEXT("saveName"), SaveName);
    
    // Parse the configurations string back into a nested JSON object to avoid string double-escaping
    TSharedPtr<FJsonObject> ConfigObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigJson);
    if (FJsonSerializer::Deserialize(Reader, ConfigObj))
    {
        PayloadObj->SetObjectField(TEXT("config"), ConfigObj);
    }

    FString RequestBody;
    TSharedRef<TJsonWriter<>> PayloadWriter = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(PayloadObj.ToSharedRef(), PayloadWriter);

    Request->SetContentAsString(RequestBody);
    Request->ProcessRequest();
}

void UFurnitureSaveManager::OnSaveResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
    {
        UE_LOG(LogTemp, Log, TEXT("[SaveSystem] Configuration successfully saved to DB."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem] Failed to save configuration to DB. Response Code: %d"), 
            Response.IsValid() ? Response->GetResponseCode() : 0);
    }
}

void UFurnitureSaveManager::LoadWorldStateForUser(const FString& UserID)
{
    if (UserID.IsEmpty()) return;

    FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
    Request->OnProcessRequestComplete().BindUObject(this, &UFurnitureSaveManager::OnLoadResponseReceived);
    Request->SetURL(FString::Printf(TEXT("%s/load?userId=%s"), *BackendAPIUrl, *UserID));
    Request->SetVerb(TEXT("GET"));
    Request->ProcessRequest();
}

void UFurnitureSaveManager::OnLoadResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (bWasSuccessful && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
    {
        FString ResponseBody = Response->GetContentAsString();
        
        // Parse root JSON from DB
        TSharedPtr<FJsonObject> RootObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);

        if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
        {
            // Extract the config object payload
            TSharedPtr<FJsonObject> ConfigObj = RootObject->GetObjectField(TEXT("config"));
            if (ConfigObj.IsValid())
            {
                FString ConfigJson;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ConfigJson);
                FJsonSerializer::Serialize(ConfigObj.ToSharedRef(), Writer);

                // Apply configuration to showroom booths
                ApplyWorldStateFromJson(ConfigJson);
            }
        }
    }
}

void UFurnitureSaveManager::ApplyWorldStateFromJson(const FString& JsonData)
{
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonData);

    if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* BoothsArray;
        if (RootObject->TryGetArrayField(TEXT("Booths"), BoothsArray))
        {
            // Map booths by Name for quick lookup
            TMap<FString, AShowroomBooth*> BoothMap;
            for (TActorIterator<AShowroomBooth> It(GetWorld()); It; ++It)
            {
                AShowroomBooth* Booth = *It;
                if (Booth)
                {
                    BoothMap.Add(Booth->GetName(), Booth);
                }
            }

            for (const TSharedPtr<FJsonValue>& Value : *BoothsArray)
            {
                TSharedPtr<FJsonObject> BoothObj = Value->AsObject();
                if (BoothObj.IsValid())
                {
                    FString BoothID = BoothObj->GetStringField(TEXT("BoothID"));
                    TSharedPtr<FJsonObject> StateObj = BoothObj->GetObjectField(TEXT("ConfigState"));

                    AShowroomBooth** FoundBooth = BoothMap.Find(BoothID);
                    if (FoundBooth && *FoundBooth && StateObj.IsValid())
                    {
                        FShowroomBoothConfigState LoadedState;
                        if (FJsonObjectConverter::JsonObjectToUStruct(StateObj.ToSharedRef(), &LoadedState))
                        {
                            // Apply state on the Server
                            (*FoundBooth)->RequestProductChange(LoadedState.ProductID);
                            
                            (*FoundBooth)->RequestComponentSelection(EFurnitureComponentType::Cabinet, LoadedState.ActiveSizeIndex, LoadedState.ActiveColorIndex);
                            (*FoundBooth)->RequestComponentSelection(EFurnitureComponentType::Countertop, LoadedState.CountertopSizeIndex, LoadedState.ActiveCountertopColorIndex);
                            (*FoundBooth)->RequestComponentSelection(EFurnitureComponentType::Closet, LoadedState.ClosetSizeIndex, LoadedState.ClosetColorIndex);
                            (*FoundBooth)->RequestComponentSelection(EFurnitureComponentType::Sink, LoadedState.SinkSizeIndex, LoadedState.SinkColorIndex);
                            (*FoundBooth)->RequestComponentSelection(EFurnitureComponentType::Faucet, LoadedState.FaucetSizeIndex, LoadedState.FaucetColorIndex);
                            (*FoundBooth)->RequestComponentSelection(EFurnitureComponentType::Mirror, LoadedState.MirrorSizeIndex, LoadedState.MirrorColorIndex);
                        }
                    }
                }
            }
        }
    }
}
```

---

## 4. Architectural Gating & Sequence

1. **Player logs in**: User enters credentials in the UE UMG panel.
2. **Server Auth Verification**: UE Server sends credentials to authentication server. On approval, server populates `AMaxiMallPlayerState::AuthenticatedUserID = VerifiedID`.
3. **Trigger Load (Automatic)**: Immediately upon writing `AuthenticatedUserID` on the server, the PlayerState class triggers `UFurnitureSaveManager::LoadWorldStateForUser(AuthenticatedUserID)`.
4. **Data Sync**: The Save Manager fetches the configuration from the database, applies the active sizes and colors to `AShowroomBooth::ActiveState` (which are replicated properties).
5. **Client Rendering**: Due to replication (`RepNotify`), all changes propagate to the player's screen over Webrtc instantly.
6. **Trigger Save (Manual)**: Player clicks "Save" in the UI. The client controller fires a Server RPC `Server_RequestSave()`. The server pulls the `UserID` from the PlayerState, gathers the booth data, and POSTs it directly to the DB API.
