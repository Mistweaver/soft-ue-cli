// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeImageOutput.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeImageOutputSpec,
	"SoftUEBridge.Capture.ImageOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgeImageOutputSpec::RunTest(const FString& Parameters)
{
	auto MakePixels = [](int32 Width, int32 Height, const FColor& Color)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Color, Width * Height);
		return Pixels;
	};

	// parses the shared arguments and writes them back unchanged
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("format"), TEXT("jpeg"));
		Args->SetStringField(TEXT("output"), TEXT("base64"));
		Args->SetNumberField(TEXT("scale"), 50.0);
		Args->SetNumberField(TEXT("width"), 320);
		Args->SetStringField(TEXT("color_mode"), TEXT("grayscale"));
		Args->SetBoolField(TEXT("cleanup_previous"), true);

		const FBridgeImageOutputOptions Options = FBridgeImageOutputOptions::FromArguments(Args);
		TestEqual(TEXT("format"), Options.Format, FString(TEXT("jpeg")));
		TestEqual(TEXT("output"), Options.OutputMode, FString(TEXT("base64")));
		TestEqual(TEXT("scale"), Options.ScalePercent, 50.0f);
		TestEqual(TEXT("width"), Options.TargetWidth, 320);
		TestEqual(TEXT("height keeps default"), Options.TargetHeight, 0);
		TestEqual(TEXT("color mode"), Options.ColorMode, FString(TEXT("grayscale")));
		TestTrue(TEXT("cleanup"), Options.bCleanupPrevious);

		TSharedPtr<FJsonObject> Forwarded = MakeShared<FJsonObject>();
		Options.WriteArguments(Forwarded);
		const FBridgeImageOutputOptions RoundTrip = FBridgeImageOutputOptions::FromArguments(Forwarded);
		TestEqual(TEXT("round trip format"), RoundTrip.Format, Options.Format);
		TestEqual(TEXT("round trip width"), RoundTrip.TargetWidth, Options.TargetWidth);
		TestEqual(TEXT("round trip color mode"), RoundTrip.ColorMode, Options.ColorMode);
	}

	// defaults apply when arguments are missing
	{
		const FBridgeImageOutputOptions Options = FBridgeImageOutputOptions::FromArguments(nullptr);
		TestEqual(TEXT("default format"), Options.Format, FString(TEXT("png")));
		TestEqual(TEXT("default output"), Options.OutputMode, FString(TEXT("file")));
		TestEqual(TEXT("default scale"), Options.ScalePercent, 100.0f);
	}

	// downscales by scale percent and never upscales
	{
		FBridgeImageOutputOptions Options;
		Options.ScalePercent = 50.0f;
		TArray<FColor> Pixels = MakePixels(8, 4, FColor::Red);
		int32 Width = 8;
		int32 Height = 4;
		FString ColorMode;
		FString Error;
		TestTrue(TEXT("half scale succeeds"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error));
		TestEqual(TEXT("half width"), Width, 4);
		TestEqual(TEXT("half height"), Height, 2);
		TestEqual(TEXT("pixel count follows size"), Pixels.Num(), 8);

		Options.ScalePercent = 400.0f;
		TestTrue(TEXT("upscale request succeeds"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error));
		TestEqual(TEXT("upscale is clamped"), Width, 4);
	}

	// target width keeps the aspect ratio
	{
		FBridgeImageOutputOptions Options;
		Options.TargetWidth = 5;
		TArray<FColor> Pixels = MakePixels(10, 6, FColor::Blue);
		int32 Width = 10;
		int32 Height = 6;
		FString ColorMode;
		FString Error;
		TestTrue(TEXT("target width succeeds"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error));
		TestEqual(TEXT("target width"), Width, 5);
		TestEqual(TEXT("derived height"), Height, 3);
	}

	// color modes convert pixels and force opaque alpha
	{
		FBridgeImageOutputOptions Options;
		Options.ColorMode = TEXT(" Monochrome ");
		TArray<FColor> Pixels = MakePixels(2, 1, FColor(250, 250, 250, 0));
		int32 Width = 2;
		int32 Height = 1;
		FString ColorMode;
		FString Error;
		TestTrue(TEXT("monochrome succeeds"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error));
		TestEqual(TEXT("color mode is normalized"), ColorMode, FString(TEXT("monochrome")));
		TestEqual(TEXT("bright pixel becomes white"), Pixels[0], FColor(255, 255, 255, 255));

		Options.ColorMode = TEXT("grayscale");
		Pixels = MakePixels(1, 1, FColor(0, 255, 0, 0));
		Width = 1;
		Height = 1;
		TestTrue(TEXT("grayscale succeeds"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error));
		TestEqual(TEXT("green becomes its luma"), Pixels[0], FColor(149, 149, 149, 255));

		Options.ColorMode = TEXT("color");
		Pixels = MakePixels(1, 1, FColor(10, 20, 30, 0));
		TestTrue(TEXT("color succeeds"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error));
		TestEqual(TEXT("color keeps channels"), Pixels[0], FColor(10, 20, 30, 255));
	}

	// rejects invalid options and mismatched dimensions
	{
		FString ColorMode;
		FString Error;
		int32 Width = 2;
		int32 Height = 2;
		TArray<FColor> Pixels = MakePixels(2, 2, FColor::Green);

		FBridgeImageOutputOptions BadColor;
		BadColor.ColorMode = TEXT("sepia");
		TestFalse(TEXT("unknown color mode"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, BadColor, ColorMode, Error));
		TestTrue(TEXT("color mode error names valid values"), Error.Contains(TEXT("grayscale")));

		FBridgeImageOutputOptions BadScale;
		BadScale.ScalePercent = 0.0f;
		TestFalse(TEXT("zero scale"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, BadScale, ColorMode, Error));

		int32 WrongWidth = 3;
		TestFalse(TEXT("mismatched pixel count"), SoftUE::ImageOutput::ApplyImageTransform(Pixels, WrongWidth, Height, FBridgeImageOutputOptions(), ColorMode, Error));
	}

	// encodes PNG with its signature and rejects mismatched sizes
	{
		const TArray<FColor> Pixels = MakePixels(4, 4, FColor::White);
		const TArray<uint8> Png = SoftUE::ImageOutput::CompressImage(Pixels, 4, 4, TEXT("png"));
		TestTrue(TEXT("png has data"), Png.Num() > 8);
		TestTrue(TEXT("png signature"), Png.Num() > 4 && Png[1] == 'P' && Png[2] == 'N' && Png[3] == 'G');
		TestEqual(TEXT("size mismatch returns empty"), SoftUE::ImageOutput::CompressImage(Pixels, 5, 4, TEXT("png")).Num(), 0);

		const TArray<uint8> Jpeg = SoftUE::ImageOutput::CompressImage(Pixels, 4, 4, TEXT("jpeg"));
		TestTrue(TEXT("jpeg signature"), Jpeg.Num() > 2 && Jpeg[0] == 0xFF && Jpeg[1] == 0xD8);
	}

	// base64 output reports sizes without writing a file
	{
		FBridgeImageOutputOptions Options;
		Options.OutputMode = TEXT("base64");
		Options.ScalePercent = 50.0f;
		TArray<FColor> Pixels = MakePixels(4, 2, FColor::Black);
		FString Error;
		const TSharedPtr<FJsonObject> Result = SoftUE::ImageOutput::EncodeCapture(Pixels, 4, 2, Options, TEXT("spec_"), Error);
		TestTrue(TEXT("encode succeeds"), Result.IsValid());
		if (Result.IsValid())
		{
			TestEqual(TEXT("mode"), Result->GetStringField(TEXT("mode")), FString(TEXT("base64")));
			TestEqual(TEXT("mime type"), Result->GetStringField(TEXT("mime_type")), FString(TEXT("image/png")));
			TestFalse(TEXT("no file path"), Result->HasField(TEXT("file_path")));
			TestEqual(TEXT("width"), static_cast<int32>(Result->GetNumberField(TEXT("width"))), 2);
			TestEqual(TEXT("original width"), static_cast<int32>(Result->GetNumberField(TEXT("original_width"))), 4);
		}
	}

	// file output writes under the prefix, and cleanup removes only that prefix
	{
		const FString Directory = SoftUE::ImageOutput::GetCaptureDirectory();
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString StalePath = Directory / TEXT("bridgespec_stale.png");
		const FString OtherPath = Directory / TEXT("bridgespecother_keep.png");
		FFileHelper::SaveStringToFile(TEXT("stale"), *StalePath);
		FFileHelper::SaveStringToFile(TEXT("keep"), *OtherPath);

		FBridgeImageOutputOptions Options;
		Options.bCleanupPrevious = true;
		TArray<FColor> Pixels = MakePixels(2, 2, FColor::Red);
		FString Error;
		const TSharedPtr<FJsonObject> Result = SoftUE::ImageOutput::EncodeCapture(Pixels, 2, 2, Options, TEXT("bridgespec_"), Error);
		TestTrue(TEXT("file encode succeeds"), Result.IsValid());

		FString FilePath;
		if (Result.IsValid() && Result->TryGetStringField(TEXT("file_path"), FilePath))
		{
			TestEqual(TEXT("file mode"), Result->GetStringField(TEXT("mode")), FString(TEXT("file")));
			TestTrue(TEXT("file name has prefix"), FPaths::GetCleanFilename(FilePath).StartsWith(TEXT("bridgespec_")));
			TestTrue(TEXT("file is written"), IFileManager::Get().FileExists(*FilePath));
		}
		else
		{
			AddError(TEXT("file mode returned no file_path"));
		}
		TestFalse(TEXT("same-prefix file is cleaned up"), IFileManager::Get().FileExists(*StalePath));
		TestTrue(TEXT("other-prefix file is kept"), IFileManager::Get().FileExists(*OtherPath));

		IFileManager::Get().Delete(*FilePath, false, true);
		IFileManager::Get().Delete(*StalePath, false, true);
		IFileManager::Get().Delete(*OtherPath, false, true);
	}

	// images that differ only after their first 1024 encoded bytes get different file names
	{
		// Noise keeps the PNG large, so the one changed pixel at the end lands well past byte 1024.
		const int32 Size = 128;
		FRandomStream Random(42);
		TArray<FColor> First;
		First.SetNumUninitialized(Size * Size);
		for (FColor& Pixel : First)
		{
			Pixel = FColor(Random.RandRange(0, 255), Random.RandRange(0, 255), Random.RandRange(0, 255), 255);
		}
		TArray<FColor> Second = First;
		Second.Last() = FColor(Second.Last().R ^ 0xFF, Second.Last().G, Second.Last().B, 255);

		const TArray<uint8> FirstBytes = SoftUE::ImageOutput::CompressImage(First, Size, Size, TEXT("png"));
		const TArray<uint8> SecondBytes = SoftUE::ImageOutput::CompressImage(Second, Size, Size, TEXT("png"));
		const bool bSharePrefix = FirstBytes.Num() > 1024 && SecondBytes.Num() > 1024
			&& FMemory::Memcmp(FirstBytes.GetData(), SecondBytes.GetData(), 1024) == 0;
		TestTrue(TEXT("precondition: encodings share their first 1024 bytes"), bSharePrefix);

		FBridgeImageOutputOptions Options;
		FString Error;
		const TSharedPtr<FJsonObject> FirstResult = SoftUE::ImageOutput::EncodeCapture(First, Size, Size, Options, TEXT("bridgespec_"), Error);
		const TSharedPtr<FJsonObject> SecondResult = SoftUE::ImageOutput::EncodeCapture(Second, Size, Size, Options, TEXT("bridgespec_"), Error);
		if (FirstResult.IsValid() && SecondResult.IsValid())
		{
			const FString FirstPath = FirstResult->GetStringField(TEXT("file_path"));
			const FString SecondPath = SecondResult->GetStringField(TEXT("file_path"));
			TestNotEqual(TEXT("different images get different file names"), FirstPath, SecondPath);
			IFileManager::Get().Delete(*FirstPath, false, true);
			IFileManager::Get().Delete(*SecondPath, false, true);
		}
		else
		{
			AddError(FString::Printf(TEXT("file encode failed: %s"), *Error));
		}
	}

	// DeletePreviousCaptures removes only files with the given prefix
	{
		const FString Directory = SoftUE::ImageOutput::GetCaptureDirectory();
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString StalePath = Directory / TEXT("bridgespec_front_stale.png");
		const FString OtherPath = Directory / TEXT("bridgespecother_keep.png");
		FFileHelper::SaveStringToFile(TEXT("stale"), *StalePath);
		FFileHelper::SaveStringToFile(TEXT("keep"), *OtherPath);

		SoftUE::ImageOutput::DeletePreviousCaptures(TEXT("bridgespec_"));

		TestFalse(TEXT("same-prefix file is deleted"), IFileManager::Get().FileExists(*StalePath));
		TestTrue(TEXT("other-prefix file is kept"), IFileManager::Get().FileExists(*OtherPath));

		IFileManager::Get().Delete(*StalePath, false, true);
		IFileManager::Get().Delete(*OtherPath, false, true);
	}

	return true;
}

#endif
