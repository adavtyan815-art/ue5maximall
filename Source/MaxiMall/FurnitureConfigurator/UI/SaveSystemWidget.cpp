// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/SaveSystemWidget.h"
#include "FurnitureConfigurator/UI/SaveHistoryItemWidget.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "UnrealClient.h"

void USaveSystemWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][UI] NativeConstruct initialized."));

    if (BackButton)
    {
        BackButton->OnClicked.AddUniqueDynamic(this, &USaveSystemWidget::OnBackClicked);
    }

    if (SaveButton)
    {
        SaveButton->OnClicked.AddUniqueDynamic(this, &USaveSystemWidget::OnSaveClicked);
    }

    if (LastSaveLoadButton)
    {
        LastSaveLoadButton->OnClicked.AddUniqueDynamic(this, &USaveSystemWidget::OnLastSaveLoadClicked);
    }

    // Immediately collapse all panels to prevent visual flicker before the web request completes
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][UI] Setting initial collapsed states for containers."));
    if (LastSaveContainer)
    {
        LastSaveContainer->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (SaveHistoryContainer)
    {
        SaveHistoryContainer->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (FirstTimeWelcomeMessage)
    {
        FirstTimeWelcomeMessage->SetVisibility(ESlateVisibility::Collapsed);
    }

    // Pull real save history from the backend server database on startup
    RefreshSaveHistory();
}

void USaveSystemWidget::OnBackClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][UI] BackButton clicked. Removing widget from viewport."));
    RemoveFromParent();
}

void USaveSystemWidget::OnSaveClicked()
{
    if (SaveNameInput)
    {
        FString SaveName = SaveNameInput->GetText().ToString();
        if (SaveName.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] Cannot save: input SaveName is empty."));
            return;
        }

        PendingSaveId = FGuid::NewGuid().ToString();
        PendingSaveName = SaveName;

        // 1. Save the layout state immediately with an empty/default thumbnail
        UE_LOG(LogTemp, Warning, TEXT("[SaveSystem] Executing initial save request immediately."));
        ExecuteSaveGame(PendingSaveId, PendingSaveName, TEXT(""));

        // 2. Request viewport capture in the background to update the thumbnail later
        UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
        if (ViewportClient)
        {
            UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][Screenshot] Requesting background viewport capture."));
            ViewportClient->OnScreenshotCaptured().AddUObject(this, &USaveSystemWidget::OnScreenshotCapturedHandler);
            FScreenshotRequest::RequestScreenshot(false);
        }

        // Clear input box
        SaveNameInput->SetText(FText::GetEmpty());
    }
}

void USaveSystemWidget::OnScreenshotTimeout()
{
    // Obsolete: timeout is no longer needed as saving is immediate.
}

void USaveSystemWidget::OnScreenshotCapturedHandler(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
    if (GEngine && GEngine->GameViewport)
    {
        GEngine->GameViewport->OnScreenshotCaptured().RemoveAll(this);
    }

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][Screenshot] Background capture callback received: %dx%d"), Width, Height);

    // Calculate crop dimensions to crop the largest possible square from the center of the viewport
    int32 CropSize = FMath::Min(Width, Height);
    int32 StartX = (Width - CropSize) / 2;
    int32 StartY = (Height - CropSize) / 2;

    int32 TargetWidth = 256;
    int32 TargetHeight = 256;
    TArray<FColor> ResizedColors;
    ResizedColors.AddUninitialized(TargetWidth * TargetHeight);

    for (int32 y = 0; y < TargetHeight; ++y)
    {
        for (int32 x = 0; x < TargetWidth; ++x)
        {
            // Map target square coordinate back to cropped viewport source coordinates
            int32 SourceX = StartX + FMath::Clamp(x * CropSize / TargetWidth, 0, CropSize - 1);
            int32 SourceY = StartY + FMath::Clamp(y * CropSize / TargetHeight, 0, CropSize - 1);

            FColor Pixel = Colors[SourceY * Width + SourceX];
            
            // Force Alpha to 255 to ensure full opacity and prevent transparent rendering issues
            Pixel.A = 255;

            ResizedColors[y * TargetWidth + x] = Pixel;
        }
    }

    TArray<uint8> CompressedBytes;
    FImageUtils::CompressImageArray(TargetWidth, TargetHeight, ResizedColors, CompressedBytes);

    FString Base64Thumbnail = FBase64::Encode(CompressedBytes);
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][Screenshot] Base64 compressed thumbnail string size: %d characters."), Base64Thumbnail.Len());

    // 3. Update the existing save with the captured thumbnail
    ExecuteSaveGame(PendingSaveId, PendingSaveName, Base64Thumbnail);
}

void USaveSystemWidget::ExecuteSaveGame(const FString& InSaveId, const FString& InSaveName, const FString& Base64Thumbnail)
{
    FString UserName = GetActiveUserName();
    FString SaveId = InSaveId;
    FString CurrentDate = FDateTime::Now().ToString(TEXT("%d.%m.%Y")); // e.g. "14.07.2026"

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] Dispatching save request: Name='%s', User='%s', SaveId='%s', Date='%s'"), 
        *InSaveName, *UserName, *SaveId, *CurrentDate);

    // Construct JSON request body
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
    JsonObject->SetStringField(TEXT("username"), UserName);
    JsonObject->SetStringField(TEXT("saveId"), SaveId);
    JsonObject->SetStringField(TEXT("saveName"), InSaveName);
    JsonObject->SetStringField(TEXT("date"), CurrentDate);
    JsonObject->SetStringField(TEXT("thumbnail"), Base64Thumbnail);

    // Gather all Showroom Booth states currently in the level
    TArray<TSharedPtr<FJsonValue>> BoothStatesJsonArray;
    TArray<AActor*> FoundBooths;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShowroomBooth::StaticClass(), FoundBooths);

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] Capturing configuration states for %d ShowroomBooths."), FoundBooths.Num());

    for (AActor* Actor : FoundBooths)
    {
        AShowroomBooth* Booth = Cast<AShowroomBooth>(Actor);
        if (Booth)
        {
            TSharedPtr<FJsonObject> BoothJson = MakeShareable(new FJsonObject());
            BoothJson->SetStringField(TEXT("boothName"), Booth->GetName());

            TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject());
            StateJson->SetStringField(TEXT("productID"), Booth->ActiveState.ProductID.ToString());
            StateJson->SetNumberField(TEXT("activeSizeIndex"), Booth->ActiveState.ActiveSizeIndex);
            StateJson->SetNumberField(TEXT("activeColorIndex"), Booth->ActiveState.ActiveColorIndex);
            StateJson->SetNumberField(TEXT("countertopSizeIndex"), Booth->ActiveState.CountertopSizeIndex);
            StateJson->SetNumberField(TEXT("activeCountertopColorIndex"), Booth->ActiveState.ActiveCountertopColorIndex);
            StateJson->SetNumberField(TEXT("closetSizeIndex"), Booth->ActiveState.ClosetSizeIndex);
            StateJson->SetNumberField(TEXT("closetColorIndex"), Booth->ActiveState.ClosetColorIndex);
            StateJson->SetNumberField(TEXT("sinkSizeIndex"), Booth->ActiveState.SinkSizeIndex);
            StateJson->SetNumberField(TEXT("sinkColorIndex"), Booth->ActiveState.SinkColorIndex);
            StateJson->SetNumberField(TEXT("faucetSizeIndex"), Booth->ActiveState.FaucetSizeIndex);
            StateJson->SetNumberField(TEXT("faucetColorIndex"), Booth->ActiveState.FaucetColorIndex);
            StateJson->SetNumberField(TEXT("mirrorSizeIndex"), Booth->ActiveState.MirrorSizeIndex);
            StateJson->SetNumberField(TEXT("mirrorColorIndex"), Booth->ActiveState.MirrorColorIndex);

            BoothJson->SetObjectField(TEXT("state"), StateJson);
            BoothStatesJsonArray.Add(MakeShareable(new FJsonValueObject(BoothJson)));
        }
    }
    JsonObject->SetArrayField(TEXT("boothStates"), BoothStatesJsonArray);

    FString RequestBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    // HTTP POST request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("%s/api/saves"), *GetBackendBaseURL()));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(5.0f);
    Request->SetContentAsString(RequestBody);

    Request->OnProcessRequestComplete().BindUObject(this, &USaveSystemWidget::OnPostSaveComplete);
    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] HTTP request dispatched to /api/saves."));
}

void USaveSystemWidget::OnLastSaveLoadClicked()
{
    if (MockNamesList.Num() > 0 && LoadedSaves.Num() > 0)
    {
        FString LastSaveId = LoadedSaves[LoadedSaves.Num() - 1]->GetStringField(TEXT("saveId"));
        UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][LOAD] Loading most recent save. ID='%s'"), *LastSaveId);
        HandleLoadSaveItem(LastSaveId);
    }
}

void USaveSystemWidget::HandleLoadSaveItem(FString SaveId)
{
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][LOAD] Selected load item with SaveId='%s' from history grid."), *SaveId);

    TSharedPtr<FJsonObject> TargetSave;
    for (const auto& Save : LoadedSaves)
    {
        if (Save->GetStringField(TEXT("saveId")) == SaveId)
        {
            TargetSave = Save;
            break;
        }
    }

    if (TargetSave.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* BoothStatesArray;
        if (TargetSave->TryGetArrayField(TEXT("boothStates"), BoothStatesArray))
        {
            APlayerController* PC = GetOwningPlayer();
            AMaxiMallPreviewController* AwsPC = Cast<AMaxiMallPreviewController>(PC);
            if (AwsPC)
            {
                TArray<AActor*> FoundBooths;
                UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShowroomBooth::StaticClass(), FoundBooths);

                UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][LOAD] Loaded save record. Applying config states to %d booths in world."), BoothStatesArray->Num());

                for (const auto& Val : *BoothStatesArray)
                {
                    TSharedPtr<FJsonObject> BoothObj = Val->AsObject();
                    if (BoothObj.IsValid())
                    {
                        FString BoothName = BoothObj->GetStringField(TEXT("boothName"));
                        TSharedPtr<FJsonObject> StateObj = BoothObj->GetObjectField(TEXT("state"));
                        if (StateObj.IsValid())
                        {
                            // Find the matching booth actor in the level
                            AShowroomBooth* TargetBooth = nullptr;
                            for (AActor* Actor : FoundBooths)
                            {
                                if (Actor->GetName() == BoothName)
                                {
                                    TargetBooth = Cast<AShowroomBooth>(Actor);
                                    break;
                                }
                            }

                            if (TargetBooth)
                            {
                                // Reconstruct FShowroomBoothConfigState
                                FShowroomBoothConfigState State;
                                State.ProductID = FName(*StateObj->GetStringField(TEXT("productID")));
                                State.ActiveSizeIndex = StateObj->GetIntegerField(TEXT("activeSizeIndex"));
                                State.ActiveColorIndex = StateObj->GetIntegerField(TEXT("activeColorIndex"));
                                State.CountertopSizeIndex = StateObj->GetIntegerField(TEXT("countertopSizeIndex"));
                                State.ActiveCountertopColorIndex = StateObj->GetIntegerField(TEXT("activeCountertopColorIndex"));
                                State.ClosetSizeIndex = StateObj->GetIntegerField(TEXT("closetSizeIndex"));
                                State.ClosetColorIndex = StateObj->GetIntegerField(TEXT("closetColorIndex"));
                                State.SinkSizeIndex = StateObj->GetIntegerField(TEXT("sinkSizeIndex"));
                                State.SinkColorIndex = StateObj->GetIntegerField(TEXT("sinkColorIndex"));
                                State.FaucetSizeIndex = StateObj->GetIntegerField(TEXT("faucetSizeIndex"));
                                State.FaucetColorIndex = StateObj->GetIntegerField(TEXT("faucetColorIndex"));
                                State.MirrorSizeIndex = StateObj->GetIntegerField(TEXT("mirrorSizeIndex"));
                                State.MirrorColorIndex = StateObj->GetIntegerField(TEXT("mirrorColorIndex"));

                                UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][LOAD] Dispatching server RPC for booth '%s'"), *BoothName);
                                AwsPC->Server_LoadBoothState(TargetBooth, State);
                            }
                        }
                    }
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[SaveSystem][LOAD] Owning Player Controller is not AMaxiMallPreviewController. Cannot load."));
            }
        }
    }
}

void USaveSystemWidget::HandleDeleteSaveItem(FString SaveId)
{
    FString UserName = GetActiveUserName();
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][DELETE] Initiating delete request for SaveId='%s' under User='%s'"), *SaveId, *UserName);

    FString RequestUrl = FString::Printf(TEXT("%s/api/saves/%s/%s"), *GetBackendBaseURL(), *UserName, *SaveId);

    // HTTP DELETE request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(RequestUrl);
    Request->SetVerb(TEXT("DELETE"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(5.0f);

    Request->OnProcessRequestComplete().BindUObject(this, &USaveSystemWidget::OnDeleteSaveComplete);
    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][DELETE] HTTP request dispatched to DELETE /api/saves."));
}

void USaveSystemWidget::SetLastSaveDetails(const FString& SaveName, const FString& Date, UTexture2D* Thumbnail)
{
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][UI] Updating Last Save display details: Name='%s', Date='%s'"), *SaveName, *Date);

    if (LastSaveName)
    {
        LastSaveName->SetText(FText::FromString(SaveName));
    }

    if (LastSaveDate)
    {
        LastSaveDate->SetText(FText::FromString(Date));
    }

    if (LastSaveThumbnail)
    {
        if (Thumbnail)
        {
            LastSaveThumbnail->SetBrushFromTexture(Thumbnail);
            // Retain user's custom layout definitions (e.g. 126x126) and scale texture inside the widget bounds
        }
        else
        {
            LastSaveThumbnail->SetBrushFromTexture(nullptr);
        }
    }
}

void USaveSystemWidget::RefreshSaveHistory()
{
    if (!SaveHistoryScrollBox)
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][GET] SaveHistoryScrollBox widget binding is missing!"));
        return;
    }

    SaveHistoryScrollBox->ClearChildren();

    if (!SaveHistoryItemClass)
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][GET] SaveHistoryItemClass is null! Assign the subwidget class in WBP_SaveSystem defaults."));
        return;
    }

    FString UserName = GetActiveUserName();
    FString RequestUrl = FString::Printf(TEXT("%s/api/saves/%s"), *GetBackendBaseURL(), *UserName);

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][GET] Fetching save history for user '%s' from URL '%s'"), *UserName, *RequestUrl);

    // HTTP GET request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(RequestUrl);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetTimeout(5.0f);

    Request->OnProcessRequestComplete().BindUObject(this, &USaveSystemWidget::OnGetSavesComplete);
    Request->ProcessRequest();

    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][GET] HTTP request dispatched to GET /api/saves/:username."));
}

void USaveSystemWidget::OnGetSavesComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][GET] HTTP GET request failed! Server is unreachable. Ensure Node.js backend is running."));
        UpdateUIVisibility(0);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][GET] HTTP GET response received. Status Code: %d"), ResponseCode);

    if (ResponseCode != 200)
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][GET] GET failed with HTTP error status: %d. Response: %s"), ResponseCode, *Response->GetContentAsString());
        UpdateUIVisibility(0);
        return;
    }

    FString ResponseString = Response->GetContentAsString();
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][GET] Payload received: %s"), *ResponseString);

    TSharedPtr<FJsonValue> JsonValue;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseString);

    if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* JsonArray;
        if (JsonValue->TryGetArray(JsonArray))
        {
            if (SaveHistoryScrollBox)
            {
                SaveHistoryScrollBox->ClearChildren();
            }
            MockNamesList.Empty();
            MockDatesList.Empty();
            LoadedSaves.Empty();
            TArray<FString> MockIds;

            for (const auto& Val : *JsonArray)
            {
                TSharedPtr<FJsonObject> Obj = Val->AsObject();
                if (Obj.IsValid())
                {
                    FString SaveId = Obj->GetStringField(TEXT("saveId"));
                    FString SaveName = Obj->GetStringField(TEXT("saveName"));
                    FString Date = Obj->GetStringField(TEXT("date"));

                    MockNamesList.Add(SaveName);
                    MockDatesList.Add(Date);
                    MockIds.Add(SaveId);
                    LoadedSaves.Add(Obj);
                }
            }

            const int32 SaveCount = MockNamesList.Num();
            UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][GET] Parsed %d save records for this user."), SaveCount);

            // Update UI element visibilities
            UpdateUIVisibility(SaveCount);

            if (SaveCount > 0)
            {
                // Decode the last save's thumbnail texture
                FString LastSaveThumbnailBase64 = LoadedSaves[SaveCount - 1]->GetStringField(TEXT("thumbnail"));
                UTexture2D* LastSaveDecodedTexture = LoadTextureFromBase64(LastSaveThumbnailBase64);

                // Update "Last Save" section with the most recent item
                SetLastSaveDetails(MockNamesList[SaveCount - 1], MockDatesList[SaveCount - 1], LastSaveDecodedTexture);

                // Build uniform grid layout
                UUniformGridPanel* GridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
                if (GridPanel)
                {
                    GridPanel->SetSlotPadding(FMargin(SaveHistorySlotPadding));

                    for (int32 i = 0; i < SaveCount; ++i)
                    {
                        USaveHistoryItemWidget* NewItem = CreateWidget<USaveHistoryItemWidget>(this, SaveHistoryItemClass);
                        if (NewItem)
                        {
                            FString ItemThumbnailBase64 = LoadedSaves[i]->GetStringField(TEXT("thumbnail"));
                            UTexture2D* ItemDecodedTexture = LoadTextureFromBase64(ItemThumbnailBase64);

                            NewItem->SetupItem(MockIds[i], MockNamesList[i], MockDatesList[i], ItemDecodedTexture);

                            // Bind callbacks
                            NewItem->OnLoadPressed.AddUniqueDynamic(this, &USaveSystemWidget::HandleLoadSaveItem);
                            NewItem->OnDeletePressed.AddUniqueDynamic(this, &USaveSystemWidget::HandleDeleteSaveItem);

                            USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                            if (SizeBox)
                            {
                                SizeBox->SetWidthOverride(SaveHistoryItemWidth);
                                SizeBox->SetHeightOverride(SaveHistoryItemHeight);
                                SizeBox->AddChild(NewItem);

                                int32 Columns = SaveHistoryColumns > 0 ? SaveHistoryColumns : 2;
                                int32 RowIdx = i / Columns;
                                int32 ColIdx = i % Columns;
                                UUniformGridSlot* GridSlot = GridPanel->AddChildToUniformGrid(SizeBox, RowIdx, ColIdx);
                                if (GridSlot)
                                {
                                    GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
                                    GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
                                }
                            }
                        }
                    }
                    SaveHistoryScrollBox->AddChild(GridPanel);
                    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][UI] Built history grid panel with %d items."), SaveCount);
                }
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][GET] Failed to parse JSON response. Response may be malformed."));
        UpdateUIVisibility(0);
    }
}

void USaveSystemWidget::OnPostSaveComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][POST] POST request failed! Server is unreachable."));
    }
    else
    {
        const int32 ResponseCode = Response->GetResponseCode();
        if (ResponseCode == 200)
        {
            UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] Save registered successfully. HTTP 200."));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[SaveSystem][POST] Save registration failed. Status: %d. Response: %s"), 
                ResponseCode, *Response->GetContentAsString());
        }
    }

    // Refresh grid list
    RefreshSaveHistory();
}

void USaveSystemWidget::OnDeleteSaveComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[SaveSystem][DELETE] DELETE request failed! Server is unreachable."));
    }
    else
    {
        const int32 ResponseCode = Response->GetResponseCode();
        if (ResponseCode == 200)
        {
            UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][DELETE] Save deleted successfully. HTTP 200."));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[SaveSystem][DELETE] Save deletion failed. Status: %d. Response: %s"), 
                ResponseCode, *Response->GetContentAsString());
        }
    }

    // Refresh grid list
    RefreshSaveHistory();
}

FString USaveSystemWidget::GetActiveUserName() const
{
    UGameInstance* GI = GetGameInstance();
    if (GI)
    {
        FProperty* NameProp = GI->GetClass()->FindPropertyByName(TEXT("UserName"));
        if (NameProp)
        {
            FString* UserNamePtr = NameProp->ContainerPtrToValuePtr<FString>(GI);
            if (UserNamePtr && !UserNamePtr->IsEmpty())
            {
                return *UserNamePtr;
            }
        }
    }
    UE_LOG(LogTemp, Error, TEXT("[SaveSystem] Failed to find logged-in Cognito 'UserName' in GameInstance. Using fallback 'guest_tester'."));
    return TEXT("guest_tester"); // fallback for testing if no active user is logged in
}

void USaveSystemWidget::UpdateUIVisibility(int32 SaveCount)
{
    bool bHasSaves = (SaveCount > 0);
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][UI] Updating container visibilities: HasSaves=%s"), bHasSaves ? TEXT("True") : TEXT("False"));

    if (LastSaveContainer)
    {
        LastSaveContainer->SetVisibility(bHasSaves ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }

    if (SaveHistoryContainer)
    {
        SaveHistoryContainer->SetVisibility(bHasSaves ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }

    if (FirstTimeWelcomeMessage)
    {
        FirstTimeWelcomeMessage->SetVisibility(bHasSaves ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    }
}

FString USaveSystemWidget::GetBackendBaseURL() const
{
    FString CommandLine = FCommandLine::Get();
    
    // 1. Check for custom BackendURL parameter
    FString CustomBackendURL;
    if (FParse::Value(*CommandLine, TEXT("-BackendURL="), CustomBackendURL))
    {
        // Strip trailing slash if present
        if (CustomBackendURL.EndsWith(TEXT("/")))
        {
            CustomBackendURL.LeftChopInline(1);
        }
        return CustomBackendURL;
    }

    // 2. Retrieve host from standard Pixel Streaming launch argument
    FString PixelStreamingURL;
    if (FParse::Value(*CommandLine, TEXT("-PixelStreamingURL="), PixelStreamingURL))
    {
        FString HostAndPort = PixelStreamingURL;
        if (HostAndPort.StartsWith(TEXT("ws://")))
        {
            HostAndPort.RightChopInline(5);
        }
        else if (HostAndPort.StartsWith(TEXT("wss://")))
        {
            HostAndPort.RightChopInline(6);
        }

        FString Host;
        FString Port;
        if (HostAndPort.Split(TEXT(":"), &Host, &Port))
        {
            return FString::Printf(TEXT("http://%s:3000"), *Host);
        }
        else
        {
            if (HostAndPort.EndsWith(TEXT("/")))
            {
                HostAndPort.LeftChopInline(1);
            }
            return FString::Printf(TEXT("http://%s:3000"), *HostAndPort);
        }
    }

    // 3. Default local fallback to AWS backend
    return TEXT("https://18-185-5-251.nip.io");
}

UTexture2D* USaveSystemWidget::LoadTextureFromBase64(const FString& Base64String)
{
    if (Base64String.IsEmpty())
    {
        return nullptr;
    }

    TArray<uint8> DecodedBytes;
    if (FBase64::Decode(Base64String, DecodedBytes))
    {
        UTexture2D* Texture = FImageUtils::ImportBufferAsTexture2D(DecodedBytes);
        if (Texture)
        {
            Texture->SRGB = true;
            Texture->UpdateResource();
            return Texture;
        }
    }
    return nullptr;
}
