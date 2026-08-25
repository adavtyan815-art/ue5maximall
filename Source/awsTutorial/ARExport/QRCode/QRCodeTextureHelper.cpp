// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/QRCode/QRCodeTextureHelper.h"
#include "ARExport/QRCode/QrCode.hpp"
#include "Engine/Texture2D.h"
#include "Rendering/Texture2DResource.h"

UTexture2D* FQRCodeTextureHelper::GenerateQRCodeTexture(const FString& TextToEncode, int32 TargetResolution, int32 BorderModules)
{
    if (TextToEncode.IsEmpty())
    {
        return nullptr;
    }

    FTCHARToUTF8 Utf8(*TextToEncode);
    qrcodegen::QrCode QR = qrcodegen::QrCode::encodeText(Utf8.Get(), qrcodegen::QrCode::Ecc::MEDIUM);

    if (!QR.isValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[QRCodeTextureHelper] Failed to encode QR Code for '%s'"), *TextToEncode);
        return nullptr;
    }

    const int32 QRSize = QR.getSize();
    const int32 TotalModules = QRSize + (BorderModules * 2);

    if (TotalModules <= 0 || TargetResolution <= 0)
    {
        return nullptr;
    }

    // Clamp resolution to a power of 2 or reasonable multiple
    const int32 TexWidth = TargetResolution;
    const int32 TexHeight = TargetResolution;
    const float ModulePixelSize = static_cast<float>(TexWidth) / static_cast<float>(TotalModules);

    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(TexWidth * TexHeight);

    const FColor BlackColor(20, 20, 25, 255);
    const FColor WhiteColor(255, 255, 255, 255);

    for (int32 Y = 0; Y < TexHeight; ++Y)
    {
        const int32 ModuleY = FMath::FloorToInt(static_cast<float>(Y) / ModulePixelSize) - BorderModules;

        for (int32 X = 0; X < TexWidth; ++X)
        {
            const int32 ModuleX = FMath::FloorToInt(static_cast<float>(X) / ModulePixelSize) - BorderModules;

            bool bIsBlack = false;
            if (ModuleX >= 0 && ModuleX < QRSize && ModuleY >= 0 && ModuleY < QRSize)
            {
                bIsBlack = QR.getModule(ModuleX, ModuleY);
            }

            Pixels[Y * TexWidth + X] = bIsBlack ? BlackColor : WhiteColor;
        }
    }

    // Create transient 2D texture
    UTexture2D* QRTexture = UTexture2D::CreateTransient(TexWidth, TexHeight, PF_B8G8R8A8);
    if (!QRTexture)
    {
        return nullptr;
    }

    QRTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
    QRTexture->SRGB = true;
    QRTexture->Filter = TextureFilter::TF_Nearest;

    // Copy pixel data into Mip 0
    void* TextureData = QRTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    QRTexture->GetPlatformData()->Mips[0].BulkData.Unlock();

    QRTexture->UpdateResource();
    return QRTexture;
}
