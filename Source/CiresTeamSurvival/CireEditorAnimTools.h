#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CireEditorAnimTools.generated.h"

class UBlendSpace;

/**
 * champion-hq: editor scripting helpers for animation assets built from Python.
 * A BlendSpace whose SampleData is written with set_editor_property keeps a stale runtime triangulation (UE only
 * rebuilds it in the BlendSpace editor), so it evaluates the wrong samples at rest (Tripo bodies stood in a walk
 * frame or with T-pose arms). ResampleBlendSpace runs the engine's validation + triangulation.
 */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireEditorAnimTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Validates the samples and rebuilds the runtime triangulation/grid; marks the package dirty. Editor only. */
    UFUNCTION(BlueprintCallable, Category = "Cire|Editor")
    static bool ResampleBlendSpace(UBlendSpace* BlendSpace);
};
