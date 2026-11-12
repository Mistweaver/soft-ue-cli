// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Output settings shared by every capture tool, parsed from the same tool arguments. */
struct SOFTUEBRIDGE_API FBridgeImageOutputOptions
{
	/** "png" or "jpeg". */
	FString Format = TEXT("png");
	/** "file" writes to the bridge temp directory; "base64" returns the encoded bytes inline. */
	FString OutputMode = TEXT("file");
	/** Percentage of the source size. Ignored when a target width or height is set. */
	float ScalePercent = 100.0f;
	int32 TargetWidth = 0;
	int32 TargetHeight = 0;
	/** "color", "grayscale", or "monochrome". */
	FString ColorMode = TEXT("color");
	/** Delete earlier files with the same prefix before writing. */
	bool bCleanupPrevious = false;

	/** Reads format, output, scale, width, height, color_mode, and cleanup_previous. */
	static FBridgeImageOutputOptions FromArguments(const TSharedPtr<FJsonObject>& Arguments);

	/** Writes the same keys FromArguments reads, for forwarding to another capture tool. */
	void WriteArguments(const TSharedPtr<FJsonObject>& Arguments) const;
};

namespace SoftUE::ImageOutput
{
	/**
	 * Downscales (never upscales) and applies the color mode in place. Alpha is forced to 255.
	 * Returns false with OutError when the options or image dimensions are invalid.
	 */
	SOFTUEBRIDGE_API bool ApplyImageTransform(
		TArray<FColor>& Pixels,
		int32& Width,
		int32& Height,
		const FBridgeImageOutputOptions& Options,
		FString& OutColorMode,
		FString& OutError);

	/**
	 * Encodes pixels as PNG, or JPEG when Format is "jpeg". Returns an empty array on failure.
	 * Alpha is written as given, so run ApplyImageTransform first for backbuffer reads.
	 */
	SOFTUEBRIDGE_API TArray<uint8> CompressImage(
		const TArray<FColor>& Pixels,
		int32 Width,
		int32 Height,
		const FString& Format);

	/** Directory that file-mode captures are written to. */
	SOFTUEBRIDGE_API FString GetCaptureDirectory();

	/**
	 * Transforms, encodes, and outputs a captured image.
	 *
	 * File mode writes <FilePrefix><hash>.<format> to GetCaptureDirectory(). The returned object holds
	 * mode, file_path or image_base64 and mime_type, format, size_bytes, width, height,
	 * original_width, original_height, and color_mode. Callers add their own message and fields.
	 * Returns nullptr with OutError set on failure.
	 */
	SOFTUEBRIDGE_API TSharedPtr<FJsonObject> EncodeCapture(
		TArray<FColor>& Pixels,
		int32 Width,
		int32 Height,
		const FBridgeImageOutputOptions& Options,
		const FString& FilePrefix,
		FString& OutError);
}
