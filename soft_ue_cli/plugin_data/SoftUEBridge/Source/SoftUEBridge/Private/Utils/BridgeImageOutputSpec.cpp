// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeImageOutput.h"
#include "Misc/AutomationTest.h"

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
		SoftUE::ImageOutput::ApplyImageTransform(Pixels, Width, Height, Options, ColorMode, Error);
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

	return true;
}

#endif
