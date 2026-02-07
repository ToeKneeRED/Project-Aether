#include "SmartLinkProxy.h"

#include "NavLinkCustomComponent.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

ASmartLinkProxy::ASmartLinkProxy()
{
    StartArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("StartArrow"));
    StartArrow->SetupAttachment(RootComponent);
    StartArrow->ArrowSize = 1.0f;

    EndArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("EndArrow"));
    EndArrow->SetupAttachment(RootComponent);
    EndArrow->ArrowSize = 1.0f;

    // Smart link defaults
    SetSmartLinkEnabled(true);
    bSmartLinkIsRelevant = true;

    // We NEVER want Simple Links
    PointLinks.Empty();
}

void ASmartLinkProxy::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Ensure Simple Links stay dead even if a BP serialized them
    PointLinks.Empty();

    // Keep smart link endpoints synced to instance-editable endpoint widgets
    SyncSmartLinkToEndpoints();
}

void ASmartLinkProxy::BeginPlay()
{
    Super::BeginPlay();

    // Runtime safety: ensure Simple Links cannot exist in-game
    PointLinks.Empty();

    // Re-sync endpoints in case anything changed between editor/runtime
    SyncSmartLinkToEndpoints();
}

#if WITH_EDITOR
void ASmartLinkProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropName = PropertyChangedEvent.Property
        ? PropertyChangedEvent.Property->GetFName()
        : NAME_None;

    const bool bMagnitudeChanged     = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, Magnitude));
    const bool bSnapModeChanged      = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, SnapMode));
    const bool bAcrossAxisChanged    = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, AcrossAxis));
    const bool bUnitsChanged         = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, UnitsToCm));
    const bool bAcrossExtraChanged   = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, AcrossExtraCm));
    const bool bAutoSnapChanged      = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, bAutoSnapOnChange));
    const bool bStartLocalChanged    = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, LinkStartLocal));
    const bool bEndLocalChanged      = (PropName == GET_MEMBER_NAME_CHECKED(ASmartLinkProxy, LinkEndLocal));

    // Always keep Simple Links empty in editor
    PointLinks.Empty();

    // If you moved the endpoint widgets manually, just refresh link data
    if (bStartLocalChanged || bEndLocalChanged)
    {
        UpdateNavLinkNow();
        return;
    }

    if (!bAutoSnapOnChange)
    {
        // Still refresh nav data when properties change
        if (bMagnitudeChanged || bSnapModeChanged || bAcrossAxisChanged || bUnitsChanged || bAcrossExtraChanged || bAutoSnapChanged)
        {
            UpdateNavLinkNow();
        }
        return;
    }

    if (bMagnitudeChanged || bSnapModeChanged || bAcrossAxisChanged || bUnitsChanged || bAcrossExtraChanged)
    {
        SnapEndToMagnitude();
    }
}
#endif

float ASmartLinkProxy::GetCodUnitsFromMagnitude(ETraversalMagnitude InMagnitude) const
{
    switch (InMagnitude)
    {
        case ETraversalMagnitude::Jump36:    return 36.f;
        case ETraversalMagnitude::Jump48:    return 48.f;
        case ETraversalMagnitude::Jump72:    return 72.f;
        case ETraversalMagnitude::Jump96:    return 96.f;
        case ETraversalMagnitude::Jump128:   return 128.f;
        case ETraversalMagnitude::Jump160:   return 160.f;
        case ETraversalMagnitude::Jump200:  return 200.f;
        case ETraversalMagnitude::Jump256:  return 256.f;
        case ETraversalMagnitude::Jump348:  return 348.f;
        case ETraversalMagnitude::Across128: return 128.f;
        case ETraversalMagnitude::Across256: return 256.f;
        default:                             return 0.f;
    }
}

FVector ASmartLinkProxy::GetAcrossOffsetRelative(float DistanceCm) const
{
    // Relative space: +X forward, +Y right
    switch (AcrossAxis)
    {
        case EAcrossAxis::Forward:  return FVector(+DistanceCm, 0.f, 0.f);
        case EAcrossAxis::Backward: return FVector(-DistanceCm, 0.f, 0.f);
        case EAcrossAxis::Right:    return FVector(0.f, +DistanceCm, 0.f);
        case EAcrossAxis::Left:     return FVector(0.f, -DistanceCm, 0.f);
        default:                    return FVector(+DistanceCm, 0.f, 0.f);
    }
}

void ASmartLinkProxy::SyncSmartLinkToEndpoints()
{
    // Never allow Simple Links
    PointLinks.Empty();

    if (!StartArrow || !EndArrow)
    {
        return;
    }

    // Use ARROWS as the authoritative endpoints (local space)
    const FVector StartRel = StartArrow->GetRelativeLocation();
    const FVector EndRel   = EndArrow->GetRelativeLocation();

    // Mirror into variables (debug/visibility only)
    LinkStartLocal = StartRel;
    LinkEndLocal   = EndRel;

    // Drive ONLY the smart link component endpoints (local space)
    if (UNavLinkCustomComponent* Comp = GetSmartLinkComp())
    {
        Comp->SetLinkData(StartRel, EndRel, ENavLinkDirection::BothWays);
    }
}


void ASmartLinkProxy::UpdateNavLinkNow()
{
    // No RerunConstructionScripts (CDO-safe + no property reset)
    SyncSmartLinkToEndpoints();

#if WITH_EDITOR
    // Visual refresh in editor
    MarkComponentsRenderStateDirty();
#endif
}

void ASmartLinkProxy::SnapEndToMagnitude()
{
    Modify();

    if (StartArrow) StartArrow->Modify();
    if (EndArrow)   EndArrow->Modify();

    if (!StartArrow || !EndArrow)
    {
        return;
    }

    if (UnitsToCm <= 0.f)
    {
        return;
    }

    const float CodUnits = GetCodUnitsFromMagnitude(Magnitude);
    if (CodUnits <= 0.f)
    {
        return;
    }

    const float BaseDistanceCm = CodUnits * UnitsToCm;

    const FVector StartRel = StartArrow->GetRelativeLocation();
    FVector EndRel = StartRel; // <-- THIS was missing

    switch (SnapMode)
    {
    case ESnapMode::None:
        return;

    case ESnapMode::Up:
        EndRel.Z += BaseDistanceCm;
        break;

    case ESnapMode::Down:
        EndRel.Z -= BaseDistanceCm;
        break;

    case ESnapMode::Across:
        {
            const float AcrossDistance = BaseDistanceCm + AcrossExtraCm;
            EndRel += GetAcrossOffsetRelative(AcrossDistance);
            break;
        }

    default:
        return;
    }

    // Move the arrow (arrows are authoritative)
    EndArrow->SetRelativeLocation(EndRel);

    // Sync smart link + clear simple links
    UpdateNavLinkNow();
}

