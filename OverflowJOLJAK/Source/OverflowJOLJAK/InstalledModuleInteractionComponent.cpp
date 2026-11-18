#include "InstalledModuleInteractionComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    // Existing Blueprint state; independent of asset folder names and redirects.
    AActor* ReadActor(const UObject* Object, FName Name)
    {
        if (!IsValid(Object)) return nullptr;
        const FObjectPropertyBase* P = FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name);
        return P ? Cast<AActor>(P->GetObjectPropertyValue_InContainer(Object)) : nullptr;
    }

    bool ReadBool(const UObject* Object, FName Name, bool Fallback = false)
    {
        const FBoolProperty* P = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
        return P ? P->GetPropertyValue_InContainer(Object) : Fallback;
    }

    double ReadNumber(const UObject* Object, FName Name, double Fallback)
    {
        const FNumericProperty* P = FindFProperty<FNumericProperty>(Object->GetClass(), Name);
        if (!P) return Fallback;
        const void* Value = P->ContainerPtrToValuePtr<void>(Object);
        return P->IsFloatingPoint() ? P->GetFloatingPointPropertyValue(Value)
            : static_cast<double>(P->GetSignedIntPropertyValue(Value));
    }

    AActor* FindInstalledModule(AActor* HitActor)
    {
        AActor* Module = HitActor;
        if (!IsValid(ReadActor(Module, TEXT("CurrentPlatform"))))
        {
            // Match BeginGrab's fallback when platform collision is hit first.
            Module = ReadActor(HitActor, TEXT("Slot0Occupant"));
        }
        if (!IsValid(Module) || !ReadBool(Module, TEXT("bCanBeMoved"))) return nullptr;
        AActor* Platform = ReadActor(Module, TEXT("CurrentPlatform"));
        const int32 Slot = static_cast<int32>(ReadNumber(Module, TEXT("CurrentSlot"), -1));
        if (!IsValid(Platform) || Slot < 0) return nullptr;
        const FName OccupantName(*FString::Printf(TEXT("Slot%dOccupant"), Slot));
        return ReadActor(Platform, OccupantName) == Module ? Module : nullptr;
    }
}

UInstalledModuleInteractionComponent::UInstalledModuleInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.05f;
}

void UInstalledModuleInteractionComponent::RemoveInteractionUI()
{
    if (InteractionWidget && WidgetViewport.IsValid() && WidgetPlayer.IsValid())
    {
        WidgetViewport->RemoveViewportWidgetForPlayer(WidgetPlayer.Get(), InteractionWidget.ToSharedRef());
    }
    InteractionWidget.Reset();
    WidgetPlayer.Reset();
    WidgetViewport.Reset();
}

void UInstalledModuleInteractionComponent::ShowInteractionUI(bool bVisible, ULocalPlayer* Player)
{
    if (InteractionWidget && (WidgetPlayer.Get() != Player || !WidgetViewport.IsValid())) RemoveInteractionUI();
    if (bVisible && !InteractionWidget && Player && Player->ViewportClient)
    {
        const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
        InteractionWidget = SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 100.f, 0.f, 0.f))
            [
                SNew(SBorder).BorderImage(White)
                .BorderBackgroundColor(FLinearColor(0.025f, 0.035f, 0.05f, 0.88f)).Padding(FMargin(12.f, 8.f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SBorder).BorderImage(White)
                        .BorderBackgroundColor(FLinearColor(0.85f, 0.92f, 1.f)).Padding(FMargin(9.f, 2.f))
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("F")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
                            .ColorAndOpacity(FLinearColor(0.025f, 0.035f, 0.05f))
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
                    [
                        SNew(STextBlock)
                        .Text(NSLOCTEXT("ModuleInteraction", "Interact", "\uC0C1\uD638\uC791\uC6A9"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
                        .ColorAndOpacity(FLinearColor::White)
                    ]
                ]
            ];
        // The overlay never intercepts gameplay input.
        InteractionWidget->SetVisibility(EVisibility::HitTestInvisible);
        WidgetPlayer = Player;
        WidgetViewport = Player->ViewportClient;
        Player->ViewportClient->AddViewportWidgetForPlayer(Player, InteractionWidget.ToSharedRef(), 20);
    }
    if (InteractionWidget) InteractionWidget->SetVisibility(bVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void UInstalledModuleInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    APawn* Pawn = Cast<APawn>(GetOwner());
    APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
    if (!Pawn || !Pawn->IsLocallyControlled() || !Player || !PC->PlayerCameraManager)
    {
        ShowInteractionUI(false, Player);
        return;
    }

    double GrabRange = 500.0;
    double TraceRange = 1500.0;
    TInlineComponentArray<UActorComponent*> Components(Pawn);
    for (const UActorComponent* Component : Components)
    {
        if (!FindFProperty<FBoolProperty>(Component->GetClass(), TEXT("bIsDragging"))) continue;
        if (ReadBool(Component, TEXT("bIsDragging")))
        {
            ShowInteractionUI(false, Player);
            return;
        }
        GrabRange = ReadNumber(Component, TEXT("GrabRange"), GrabRange);
        TraceRange = ReadNumber(Component, TEXT("PlacementRange"), TraceRange);
        break;
    }

    const FVector Start = PC->PlayerCameraManager->GetCameraLocation();
    const FVector End = Start + PC->PlayerCameraManager->GetCameraRotation().Vector() * TraceRange;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(InstalledModuleInteractionWidget), false, Pawn);
    FHitResult Hit;
    AActor* Module = nullptr;
    if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
        FCollisionShape::MakeSphere(10.f), Params))
    {
        Module = FindInstalledModule(Hit.GetActor());
    }
    // Keep the hint close to the facility without changing the existing grab range.
    const double InteractionRange = FMath::Min(GrabRange, 200.0);
    const bool bVisible = Module && FVector::DistSquared(Pawn->GetActorLocation(), Module->GetActorLocation())
        <= FMath::Square(InteractionRange);
    ShowInteractionUI(bVisible, Player);
}

void UInstalledModuleInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveInteractionUI();
    Super::EndPlay(EndPlayReason);
}
