#include "PaintHUD.h"

#include "RainPaintGame.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"

void APaintHUD::DrawHUD()
{
	Super::DrawHUD();
	ARainPaintGame* Game = nullptr;
	for (TActorIterator<ARainPaintGame> It(GetWorld()); It; ++It)
	{
		Game = *It;
		break;
	}
	if (!Game || !Canvas || !GEngine)
	{
		return;
	}
	// Sized with the window's height (1080 p = 1). A narrow column in the corner, clear of the level in the middle.
	const float S = Canvas->ClipY / 1080.0f;
	UFont* Font = GEngine->GetMediumFont();
	const FLinearColor Ink = Game->GetInkColor();
	const FLinearColor Text(0.13f, 0.11f, 0.16f);
	const FLinearColor Soft(0.33f, 0.3f, 0.36f, 0.85f);
	const FLinearColor Track(0.2f, 0.15f, 0.2f, 0.12f);
	auto Number = [](int32 N) { return FText::AsNumber(N).ToString(); };
	const float X = 30.0f * S;
	const float W = 320.0f * S;
	float Y = 26.0f * S;

	// The round: the level, then the clock or how it ended.
	FString Round;
	switch (Game->GetPhase())
	{
	case EPaintPhase::Ready:
		Round = FString::Printf(TEXT("%s   aim, then Enter"), *Game->GetLevelName());
		break;
	case EPaintPhase::Painting:
		Round = FString::Printf(TEXT("%s   %.1f s"), *Game->GetLevelName(), Game->GetClock());
		break;
	case EPaintPhase::Done:
		Round = Game->IsOutOfRain()
			? FString::Printf(TEXT("%s   out of rain   (R)"), *Game->GetLevelName())
			: FString::Printf(TEXT("%s   %.1f s  %s   (Enter)"), *Game->GetLevelName(), Game->GetClock(),
				Game->GetClock() <= Game->GetParSeconds() ? TEXT("***") : Game->GetClock() <= 1.5f * Game->GetParSeconds() ? TEXT("**") : TEXT("*"));
		break;
	}
	DrawText(Round, Text, X, Y, Font, 1.2f * S);
	Y += 36.0f * S;

	// Two bars: the paint towards the goal (a tick at the goal), the rain left in the pool.
	const float Goal = Game->CoverageGoal;
	const float Painted = FMath::Clamp(Game->GetCoverage(), 0.0f, 1.0f);
	DrawText(FString::Printf(TEXT("painted %.0f%%   goal %.0f%%   par %.0f s"), 100.0f * Painted, 100.0f * Goal, Game->GetParSeconds()), Soft, X, Y, Font, 0.9f * S);
	Y += 24.0f * S;
	DrawRect(Track, X, Y, W, 7.0f * S);
	DrawRect(FLinearColor(Ink.R, Ink.G, Ink.B, 1.0f), X, Y, W * Painted, 7.0f * S);
	DrawRect(Text, X + W * Goal - 1.0f * S, Y - 3.0f * S, 2.0f * S, 13.0f * S);
	Y += 18.0f * S;
	const bool bBalls = Game->GetPaintBallMode();
	const int32 Size = bBalls ? Game->GetPaintBallPool() : Game->GetPoolSize();
	const int32 Left = bBalls ? Game->GetPaintBallsLeft() : Game->GetPoolLeft();
	DrawText(FString::Printf(TEXT("%s left %s / %s"), bBalls ? TEXT("paint balls") : TEXT("rain"), *Number(Left), *Number(Size)), Soft, X, Y, Font, 0.9f * S);
	Y += 24.0f * S;
	DrawRect(Track, X, Y, W, 7.0f * S);
	DrawRect(FLinearColor(Ink.R, Ink.G, Ink.B, 0.55f), X, Y, W * (Size > 0 ? float(Left) / Size : 0.0f), 7.0f * S);
	Y += 16.0f * S;
	DrawText(bBalls
		? FString::Printf(TEXT("System 2: %s paint balls %s   puddles %s"), Game->GetPaintBallsOnGpu() ? TEXT("GPU") : TEXT("CPU"), *Number(Game->GetLivePaintBallCount()), *Number(Game->GetNumPuddles()))
		: FString::Printf(TEXT("System 1: GPU drops %s   big %s   puddles %s"), *Number(Game->GetLiveParticleCount()), *Number(Game->GetBigActive()), *Number(Game->GetNumPuddles())),
		Soft, X, Y, Font, 0.75f * S);
	Y += 30.0f * S;

	// Before a round: buttons for the level (or L), the system (or Tab) and, for the rain, the drops' look (or F).
	if (Game->GetPhase() != EPaintPhase::Painting)
	{
		const float BW = 250.0f * S, BH = 28.0f * S;
		auto Button = [&](const FString& Label, float At)
		{
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f), X, At, BW, BH);
			DrawRect(FLinearColor(Ink.R, Ink.G, Ink.B, 1.0f), X, At, 4.0f * S, BH);
			DrawText(Label, Text, X + 14.0f * S, At + 5.0f * S, Font, 0.85f * S);
			return FBox2D(FVector2D(X, At), FVector2D(X + BW, At + BH));
		};
		const int32 NumLevels = Game->GetNumLevels();
		Game->SetLevelButton(Button(FString::Printf(TEXT("level %d/%d: %s   (L)"), Game->GetLevelIndex() + 1, NumLevels, *Game->GetLevelName()), Y));
		// The list, open: one row a level to the right of the panel's column (number, name, system, par); the current
		// one marked with the ink.
		if (Game->IsLevelListOpen())
		{
			const float LX = X + W + 16.0f * S, LW = 300.0f * S, RH = 28.0f * S, Gap = 3.0f * S;
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.35f), LX - 6.0f * S, Y - 6.0f * S, LW + 12.0f * S, NumLevels * (RH + Gap) - Gap + 12.0f * S);
			for (int32 i = 0; i < NumLevels; ++i)
			{
				const float RY = Y + i * (RH + Gap);
				const bool bCurrent = i == Game->GetLevelIndex();
				DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, bCurrent ? 0.95f : 0.7f), LX, RY, LW, RH);
				if (bCurrent)
				{
					DrawRect(FLinearColor(Ink.R, Ink.G, Ink.B, 1.0f), LX, RY, 4.0f * S, RH);
				}
				DrawText(FString::Printf(TEXT("%d   %s"), i + 1, *Game->GetLevelNameAt(i)), Text, LX + 14.0f * S, RY + 5.0f * S, Font, 0.85f * S);
				DrawText(FString::Printf(TEXT("%s   par %.0f s"), Game->IsBallLevel(i) ? TEXT("balls") : TEXT("rain"), Game->GetParSecondsAt(i)),
					Soft, LX + 170.0f * S, RY + 6.0f * S, Font, 0.75f * S);
				Game->SetLevelRow(i, FBox2D(FVector2D(LX, RY), FVector2D(LX + LW, RY + RH)));
			}
		}
		Y += BH + 8.0f * S;
		Game->SetSystemButton(Button(FString::Printf(TEXT("play with: %s   (Tab / M)"), bBalls ? TEXT("paint balls") : TEXT("rain")), Y));
		Y += BH + 8.0f * S;
		Game->SetLookButton(bBalls ? FBox2D(ForceInit) : Button(FString::Printf(TEXT("drops: %s   (F)"), Game->GetFilmDrops() ? TEXT("film") : TEXT("paint")), Y));
		Y += bBalls ? 0.0f : BH + 8.0f * S;
	}
	else
	{
		Game->SetLevelButton(FBox2D(ForceInit));
		Game->SetSystemButton(FBox2D(ForceInit));
		Game->SetLookButton(FBox2D(ForceInit));
	}

	// Sliders: the pool's size before a round, the drops' opacity at any time (the rain only).
	auto Slider = [&](int32 Id, bool bShown)
	{
		if (!bShown)
		{
			Game->SetSliderRect(Id, FBox2D(ForceInit));
			return;
		}
		Y += 10.0f * S;
		DrawText(Game->GetSliderLabel(Id), Soft, X, Y, Font, 0.85f * S);
		Y += 24.0f * S;
		const float V = Game->GetSliderValue(Id);
		DrawRect(Track, X, Y, W, 6.0f * S);
		DrawRect(FLinearColor(Ink.R, Ink.G, Ink.B, 0.8f), X, Y, W * V, 6.0f * S);
		DrawRect(Text, X + W * V - 4.0f * S, Y - 6.0f * S, 8.0f * S, 18.0f * S);
		Game->SetSliderRect(Id, FBox2D(FVector2D(X, Y - 8.0f * S), FVector2D(X + W, Y + 14.0f * S)));
		Y += 16.0f * S;
	};
	Slider(0, Game->GetPhase() != EPaintPhase::Painting);
	Slider(1, !bBalls);

	// The demo's caption, centred along the bottom, in place of the controls.
	const FString Caption = Game->GetDemoCaption();
	if (!Caption.IsEmpty())
	{
		const float Scale = 1.15f * S;
		float CW = 0.0f, CH = 0.0f;
		Canvas->TextSize(Font, Caption, CW, CH, Scale, Scale);
		const float CX = 0.5f * (Canvas->ClipX - CW), CY = Canvas->ClipY - 70.0f * S;
		DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), CX - 18.0f * S, CY - 9.0f * S, CW + 36.0f * S, CH + 18.0f * S);
		DrawRect(FLinearColor(Ink.R, Ink.G, Ink.B, 1.0f), CX - 18.0f * S, CY - 9.0f * S, 4.0f * S, CH + 18.0f * S);
		DrawText(Caption, Text, CX, CY, Font, Scale);
		return;
	}
	// The controls, small, along the bottom left.
	DrawText(Game->GetFreeze() > 0.5f ? FString(TEXT("the rain is held   (let go of the ball)"))
		: FString(TEXT("drag the ball: turn the direction    hold it still: stop the rain and the clock    Enter: start    R: again    Q/E: view    wheel: zoom    L or 1-8: level")),
		Soft, X, Canvas->ClipY - 44.0f * S, Font, 0.8f * S);
}
