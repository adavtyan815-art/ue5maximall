// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

class AWSTUTORIAL_API FQRCodeTextureHelper
{
public:
    /**
     * Generates a dynamic UTexture2D representing the given text / URL as a QR code.
     * @param TextToEncode The string (e.g. WebAR URL) to encode in the QR code.
     * @param TargetResolution Image width/height in pixels (default: 1024).
     * @param BorderModules Margin width in QR modules around the code (default: 6 for maximum camera contrast).
     * @return A newly created transient UTexture2D ready for UMG brush rendering, or nullptr on failure.
     */
    static UTexture2D* GenerateQRCodeTexture(const FString& TextToEncode, int32 TargetResolution = 1024, int32 BorderModules = 6);
};
