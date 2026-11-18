#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InstalledModuleInteractionComponent.generated.h"

class SWidget;
class ULocalPlayer;
class UGameViewportClient;

/** Local screen hint for a module occupying a platform slot. Does not bind input. */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class OVERFLOWJOLJAK_API UInstalledModuleInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInstalledModuleInteractionComponent();

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void ShowInteractionUI(bool bVisible, ULocalPlayer* Player);
    void RemoveInteractionUI();

    TSharedPtr<SWidget> InteractionWidget;
    TWeakObjectPtr<ULocalPlayer> WidgetPlayer;
    TWeakObjectPtr<UGameViewportClient> WidgetViewport;
};
