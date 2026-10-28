// Project Rain - Paint Valley: a narrow column at the top left (canvas, no widgets), clear of the level. It reads the
// game actor: the round's clock, the paint towards the goal and the rain left in the pool as bars, the drops falling
// and the particles alive, buttons and sliders (the game handles their clicks); the controls along the bottom left.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PaintHUD.generated.h"

UCLASS()
class PROJECTRAIN_API APaintHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
