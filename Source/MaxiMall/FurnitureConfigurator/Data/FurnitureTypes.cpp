// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "Engine/DataTable.h"

TArray<FString> UFurnitureEditorHelper::GetCountertopOptions()
{
    TArray<FString> Options;
    Options.Add(TEXT("None"));
#if WITH_EDITOR
    UDataTable* DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/DT/DT_SharedCountertops.DT_SharedCountertops")));
    if (DataTable)
    {
        TArray<FName> RowNames = DataTable->GetRowNames();
        for (const FName& RowName : RowNames)
        {
            Options.Add(RowName.ToString());
        }
    }
#endif
    return Options;
}

TArray<FString> UFurnitureEditorHelper::GetSinkOptions()
{
    TArray<FString> Options;
    Options.Add(TEXT("None"));
#if WITH_EDITOR
    UDataTable* DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/DT/DT_SharedSinks.DT_SharedSinks")));
    if (DataTable)
    {
        TArray<FName> RowNames = DataTable->GetRowNames();
        for (const FName& RowName : RowNames)
        {
            Options.Add(RowName.ToString());
        }
    }
#endif
    return Options;
}

TArray<FString> UFurnitureEditorHelper::GetFaucetOptions()
{
    TArray<FString> Options;
    Options.Add(TEXT("None"));
#if WITH_EDITOR
    UDataTable* DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/DT/DT_SharedFaucets.DT_SharedFaucets")));
    if (!DataTable)
    {
        DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/DT/AllowedFaucetIDs.AllowedFaucetIDs")));
    }
    if (DataTable)
    {
        TArray<FName> RowNames = DataTable->GetRowNames();
        for (const FName& RowName : RowNames)
        {
            Options.Add(RowName.ToString());
        }
    }
#endif
    return Options;
}

TArray<FString> UFurnitureEditorHelper::GetMirrorOptions()
{
    TArray<FString> Options;
    Options.Add(TEXT("None"));
#if WITH_EDITOR
    UDataTable* DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/DT/DT_SharedMirrors.DT_SharedMirrors")));
    if (!DataTable)
    {
        DataTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, TEXT("/Game/DT/AllowedMirrorIDs.AllowedMirrorIDs")));
    }
    if (DataTable)
    {
        TArray<FName> RowNames = DataTable->GetRowNames();
        for (const FName& RowName : RowNames)
        {
            Options.Add(RowName.ToString());
        }
    }
#endif
    return Options;
}
