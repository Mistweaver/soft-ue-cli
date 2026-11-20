// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeImageOutput.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString ImageOutputNormalizeColorMode(FString ColorMode)
	{
		ColorMode.TrimStartAndEndInline();
		ColorMode.ToLowerInline();
		return ColorMode.IsEmpty() ? TEXT("color") : ColorMode;
	}

	FIntPoint ImageOutputResolveSize(int32 Width, int32 Height, const FBridgeImageOutputOptions& Options)
	{
		float Scale = 1.0f;
		if (Options.TargetWidth > 0 && Options.TargetHeight > 0)
		{
			Scale = FMath::Min(
				static_cast<float>(Options.TargetWidth) / static_cast<float>(Width),
				static_cast<float>(Options.TargetHeight) / static_cast<float>(Height));
		}
		else if (Options.TargetWidth > 0)
		{
			Scale = static_cast<float>(Options.TargetWidth) / static_cast<float>(Width);
		}
		else if (Options.TargetHeight > 0)
		{
			Scale = static_cast<float>(Options.TargetHeight) / static_cast<float>(Height);
		}
		else
		{
			Scale = Options.ScalePercent / 100.0f;
		}

		Scale = FMath::Clamp(Scale, 0.0f, 1.0f);
		if (Scale >= 1.0f)
		{
			return FIntPoint(Width, Height);
		}

		return FIntPoint(
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(Width) * Scale)),
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(Height) * Scale)));
	}

	/** Nearest-neighbor resample. */
	void ImageOutputResize(TArray<FColor>& Pixels, int32& Width, int32& Height, const FIntPoint& TargetSize)
	{
		if (TargetSize.X == Width && TargetSize.Y == Height)
		{
			return;
		}

		TArray<FColor> Resized;
		Resized.SetNumUninitialized(TargetSize.X * TargetSize.Y);

		for (int32 Y = 0; Y < TargetSize.Y; ++Y)
		{
			const int32 SourceY = FMath::Clamp(
				FMath::FloorToInt((static_cast<float>(Y) + 0.5f) * static_cast<float>(Height) / static_cast<float>(TargetSize.Y)),
				0,
				Height - 1);
			for (int32 X = 0; X < TargetSize.X; ++X)
			{
				const int32 SourceX = FMath::Clamp(
					FMath::FloorToInt((static_cast<float>(X) + 0.5f) * static_cast<float>(Width) / static_cast<float>(TargetSize.X)),
					0,
					Width - 1);
				Resized[Y * TargetSize.X + X] = Pixels[SourceY * Width + SourceX];
			}
		}

		Pixels = MoveTemp(Resized);
		Width = TargetSize.X;
		Height = TargetSize.Y;
	}

	void ImageOutputApplyColorMode(TArray<FColor>& Pixels, const FString& ColorMode)
	{
		for (FColor& Pixel : Pixels)
		{
			// DX12/Vulkan backbuffers may have undefined alpha (often 0), which would make the PNG transparent.
			Pixel.A = 255;
			if (ColorMode == TEXT("color"))
			{
				continue;
			}

			const uint8 Luma = static_cast<uint8>((77 * Pixel.R + 150 * Pixel.G + 29 * Pixel.B) >> 8);
			const uint8 Value = (ColorMode == TEXT("monochrome"))
				? (Luma >= 128 ? 255 : 0)
				: Luma;
			Pixel.R = Value;
			Pixel.G = Value;
			Pixel.B = Value;
		}
	}
}

FBridgeImageOutputOptions FBridgeImageOutputOptions::FromArguments(const TSharedPtr<FJsonObject>& Arguments)
{
	FBridgeImageOutputOptions Options;
	if (!Arguments.IsValid())
	{
		return Options;
	}

	Arguments->TryGetStringField(TEXT("format"), Options.Format);
	Arguments->TryGetStringField(TEXT("output"), Options.OutputMode);
	double Scale = 0.0;
	if (Arguments->TryGetNumberField(TEXT("scale"), Scale))
	{
		Options.ScalePercent = static_cast<float>(Scale);
	}
	Arguments->TryGetNumberField(TEXT("width"), Options.TargetWidth);
	Arguments->TryGetNumberField(TEXT("height"), Options.TargetHeight);
	Arguments->TryGetStringField(TEXT("color_mode"), Options.ColorMode);
	Arguments->TryGetBoolField(TEXT("cleanup_previous"), Options.bCleanupPrevious);
	return Options;
}

void FBridgeImageOutputOptions::WriteArguments(const TSharedPtr<FJsonObject>& Arguments) const
{
	if (!Arguments.IsValid())
	{
		return;
	}

	Arguments->SetStringField(TEXT("format"), Format);
	Arguments->SetStringField(TEXT("output"), OutputMode);
	Arguments->SetNumberField(TEXT("scale"), ScalePercent);
	Arguments->SetNumberField(TEXT("width"), TargetWidth);
	Arguments->SetNumberField(TEXT("height"), TargetHeight);
	Arguments->SetStringField(TEXT("color_mode"), ColorMode);
	Arguments->SetBoolField(TEXT("cleanup_previous"), bCleanupPrevious);
}

bool SoftUE::ImageOutput::ApplyImageTransform(
	TArray<FColor>& Pixels,
	int32& Width,
	int32& Height,
	const FBridgeImageOutputOptions& Options,
	FString& OutColorMode,
	FString& OutError)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		OutError = TEXT("Invalid image dimensions");
		return false;
	}

	if (Options.ScalePercent <= 0.0f)
	{
		OutError = TEXT("'scale' must be greater than 0");
		return false;
	}

	if (Options.TargetWidth < 0 || Options.TargetHeight < 0)
	{
		OutError = TEXT("'width' and 'height' must be greater than or equal to 0");
		return false;
	}

	OutColorMode = ImageOutputNormalizeColorMode(Options.ColorMode);
	if (OutColorMode != TEXT("color") &&
		OutColorMode != TEXT("grayscale") &&
		OutColorMode != TEXT("monochrome"))
	{
		OutError = FString::Printf(
			TEXT("Invalid color_mode '%s'. Valid values: color, grayscale, monochrome"),
			*Options.ColorMode);
		return false;
	}

	ImageOutputResize(Pixels, Width, Height, ImageOutputResolveSize(Width, Height, Options));
	ImageOutputApplyColorMode(Pixels, OutColorMode);
	return true;
}

TArray<uint8> SoftUE::ImageOutput::CompressImage(
	const TArray<FColor>& Pixels,
	int32 Width,
	int32 Height,
	const FString& Format)
{
	if (Width <= 0 || Height <= 0)
	{
		return {};
	}

	// The pixel count can diverge from the dimensions if a viewport resizes mid-capture.
	const int64 ExpectedPixelCount = static_cast<int64>(Width) * static_cast<int64>(Height);
	if (Pixels.Num() != ExpectedPixelCount || ExpectedPixelCount > MAX_int32 / 4)
	{
		return {};
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	const bool bJpeg = Format == TEXT("jpeg");
	TSharedPtr<IImageWrapper> ImageWrapper =
		ImageWrapperModule.CreateImageWrapper(bJpeg ? EImageFormat::JPEG : EImageFormat::PNG);
	if (!ImageWrapper.IsValid())
	{
		return {};
	}

	// FColor is BGRA in memory, so the pixels are passed through without reordering.
	if (!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * 4, Width, Height, ERGBFormat::BGRA, 8))
	{
		return {};
	}

	const int32 Quality = bJpeg ? 85 : 0;
	TArray<uint8> Result;
	Result.Append(ImageWrapper->GetCompressed(Quality));
	return Result;
}

FString SoftUE::ImageOutput::GetCaptureDirectory()
{
	return FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("soft-ue-bridge"));
}

void SoftUE::ImageOutput::DeletePreviousCaptures(const FString& FilePrefix)
{
	const FString Directory = GetCaptureDirectory();
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *(Directory / (FilePrefix + TEXT("*"))), true, false);

	for (const FString& FileName : FoundFiles)
	{
		IFileManager::Get().Delete(*FPaths::Combine(Directory, FileName), false, true);
	}
}

TSharedPtr<FJsonObject> SoftUE::ImageOutput::EncodeCapture(
	TArray<FColor>& Pixels,
	int32 Width,
	int32 Height,
	const FBridgeImageOutputOptions& Options,
	const FString& FilePrefix,
	FString& OutError)
{
	const int32 OriginalWidth = Width;
	const int32 OriginalHeight = Height;
	FString ColorMode;
	if (!ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, OutError))
	{
		return nullptr;
	}

	const TArray<uint8> ImageData = CompressImage(Pixels, Width, Height, Options.Format);
	if (ImageData.Num() == 0)
	{
		OutError = TEXT("Failed to compress screenshot image");
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	if (Options.OutputMode == TEXT("base64"))
	{
		Result->SetStringField(TEXT("image_base64"), FBase64::Encode(ImageData));
		Result->SetStringField(TEXT("mime_type"), Options.Format == TEXT("jpeg") ? TEXT("image/jpeg") : TEXT("image/png"));
		Result->SetStringField(TEXT("mode"), TEXT("base64"));
	}
	else
	{
		const FString Directory = GetCaptureDirectory();
		IFileManager::Get().MakeDirectory(*Directory, true);
		if (Options.bCleanupPrevious)
		{
			DeletePreviousCaptures(FilePrefix);
		}

		const FString Hash = FMD5::HashBytes(ImageData.GetData(), ImageData.Num());
		const FString FilePath = FPaths::Combine(
			Directory,
			FString::Printf(TEXT("%s%s.%s"), *FilePrefix, *Hash.Left(8), *Options.Format));
		if (!FFileHelper::SaveArrayToFile(ImageData, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to write capture to %s"), *FilePath);
			return nullptr;
		}

		Result->SetStringField(TEXT("mode"), TEXT("file"));
		Result->SetStringField(TEXT("file_path"), FilePath);
	}

	Result->SetStringField(TEXT("format"), Options.Format);
	Result->SetNumberField(TEXT("size_bytes"), ImageData.Num());
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetNumberField(TEXT("original_width"), OriginalWidth);
	Result->SetNumberField(TEXT("original_height"), OriginalHeight);
	Result->SetStringField(TEXT("color_mode"), ColorMode);
	return Result;
}
