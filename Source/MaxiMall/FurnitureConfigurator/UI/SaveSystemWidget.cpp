// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/SaveSystemWidget.h"
#include "FurnitureConfigurator/UI/SaveHistoryItemWidget.h"
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

        FString UserName = GetActiveUserName();
        FString SaveId = FGuid::NewGuid().ToString();
        FString CurrentDate = FDateTime::Now().ToString(TEXT("%d.%m.%Y")); // e.g. "14.07.2026"

        UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] Initiating save request: Name='%s', User='%s', SaveId='%s', Date='%s'"), 
            *SaveName, *UserName, *SaveId, *CurrentDate);

        // Construct JSON request body
        TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
        JsonObject->SetStringField(TEXT("username"), UserName);
        JsonObject->SetStringField(TEXT("saveId"), SaveId);
        JsonObject->SetStringField(TEXT("saveName"), SaveName);
        JsonObject->SetStringField(TEXT("date"), CurrentDate);

        FString RequestBody;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

        // HTTP POST request
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(FString::Printf(TEXT("%s/api/saves"), *GetBackendBaseURL()));
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetContentAsString(RequestBody);

        Request->OnProcessRequestComplete().BindUObject(this, &USaveSystemWidget::OnPostSaveComplete);
        Request->ProcessRequest();

        UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][POST] HTTP request dispatched to /api/saves."));

        // Clear input box
        SaveNameInput->SetText(FText::GetEmpty());
    }
}

void USaveSystemWidget::OnLastSaveLoadClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][LOAD] Clicked Load on last save panel. Loading trigger pending."));
}

void USaveSystemWidget::HandleLoadSaveItem(FString SaveId)
{
    UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][LOAD] Selected load item with SaveId='%s' from history grid."), *SaveId);
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

    if (LastSaveThumbnail && Thumbnail)
    {
        LastSaveThumbnail->SetBrushFromTexture(Thumbnail);
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
            MockNamesList.Empty();
            MockDatesList.Empty();
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
                }
            }

            const int32 SaveCount = MockNamesList.Num();
            UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][GET] Parsed %d save records for this user."), SaveCount);

            // Update UI element visibilities
            UpdateUIVisibility(SaveCount);

            if (SaveCount > 0)
            {
                // Update "Last Save" section with the most recent item
                SetLastSaveDetails(MockNamesList[SaveCount - 1], MockDatesList[SaveCount - 1], nullptr);

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
                            NewItem->SetupItem(MockIds[i], MockNamesList[i], MockDatesList[i], nullptr);

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
    FString PixelStreamingURL;

    // Retrieve host from standard Pixel Streaming launch argument
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

    // Default local fallback to PC 1 IP address
    return TEXT("http://192.168.10.138:3000");
}
