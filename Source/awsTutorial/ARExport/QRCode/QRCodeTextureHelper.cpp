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
    // Use generous quiet zone (at least 8 modules) for guaranteed camera detection
    const int32 Margin = FMath::Max(BorderModules, 8);
    const int32 TotalModules = QRSize + (Margin * 2);

    const int32 TexWidth = (TargetResolution > 0) ? TargetResolution : 1024;
    const int32 TexHeight = (TargetResolution > 0) ? TargetResolution : 1024;
    const float ModulePixelSize = static_cast<float>(TexWidth) / static_cast<float>(TotalModules);

    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(TexWidth * TexHeight);

    const FColor PureBlack(0, 0, 0, 255);
    const FColor PureWhite(255, 255, 255, 255);

    for (int32 Y = 0; Y < TexHeight; ++Y)
    {
        const int32 ModuleY = FMath::FloorToInt(static_cast<float>(Y) / ModulePixelSize) - Margin;

        for (int32 X = 0; X < TexWidth; ++X)
        {
            const int32 ModuleX = FMath::FloorToInt(static_cast<float>(X) / ModulePixelSize) - Margin;

            bool bIsBlack = false;
            if (ModuleX >= 0 && ModuleX < QRSize && ModuleY >= 0 && ModuleY < QRSize)
            {
                bIsBlack = QR.getModule(ModuleX, ModuleY);
            }

            Pixels[Y * TexWidth + X] = bIsBlack ? PureBlack : PureWhite;
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
