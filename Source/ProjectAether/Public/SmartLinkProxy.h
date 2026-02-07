#pragma once

#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "Components/ArrowComponent.h"

#include "SmartLinkProxy.generated.h"

UENUM(BlueprintType)
enum class ETraversalMagnitude : uint8
{
    Jump36     UMETA(DisplayName="Jump 36"),
    Jump48     UMETA(DisplayName="Jump 48"),
    Jump72     UMETA(DisplayName="Jump 72"),
    Jump96     UMETA(DisplayName="Jump 96"),
    Jump128    UMETA(DisplayName="Jump 128"),
    Jump160    UMETA(DisplayName="Jump 160"),
    Jump200    UMETA(DisplayName="Jump 200"),
    Jump256    UMETA(DisplayName="Jump 256"),
    Jump348    UMETA(DisplayName="Jump 348"),
    Across128  UMETA(DisplayName="Across 128"),
    Across256  UMETA(DisplayName="Across 256"),

    
};

UENUM(BlueprintType)
enum class ESnapMode : uint8
{
    None   UMETA(DisplayName="None (Manual)"),
    Up     UMETA(DisplayName="Up"),
    Down   UMETA(DisplayName="Down"),
    Across UMETA(DisplayName="Across")
};

UENUM(BlueprintType)
enum class EAcrossAxis : uint8
{
    Forward  UMETA(DisplayName="Forward (+X)"),
    Right    UMETA(DisplayName="Right (+Y)"),
    Left     UMETA(DisplayName="Left (-Y)"),
    Backward UMETA(DisplayName="Backward (-X)")
};

UCLASS()
class PROJECTAETHER_API ASmartLinkProxy : public ANavLinkProxy
{
    GENERATED_BODY()

public:
    ASmartLinkProxy();

    // These are just visuals. Your real editable points are LinkStartLocal/LinkEndLocal.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SmartLink", AdvancedDisplay)
    UArrowComponent* StartArrow;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SmartLink", AdvancedDisplay)
    UArrowComponent* EndArrow;

    // Traversal setup
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal")
    ETraversalMagnitude Magnitude = ETraversalMagnitude::Jump96;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal")
    ESnapMode SnapMode = ESnapMode::Up;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal")
    EAcrossAxis AcrossAxis = EAcrossAxis::Forward;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal")
    bool bAutoSnapOnChange = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal")
    float UnitsToCm = 2.54f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traversal")
    float AcrossExtraCm = 0.0f;

    // Instance-editable endpoints (local space) shown as viewport gizmos
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Traversal|Endpoints")
    FVector LinkStartLocal = FVector(0.f, -50.f, 0.f);

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Traversal|Endpoints")
    FVector LinkEndLocal = FVector(0.f, 50.f, 0.f);

    // Buttons
    UFUNCTION(CallInEditor, Category="Traversal")
    void SnapEndToMagnitude();

    UFUNCTION(CallInEditor, Category="Traversal")
    void UpdateNavLinkNow();

protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    float GetCodUnitsFromMagnitude(ETraversalMagnitude InMagnitude) const;
    FVector GetAcrossOffsetRelative(float DistanceCm) const;

    void SyncSmartLinkToEndpoints();
};
