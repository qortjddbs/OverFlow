#include "Craft/PlacedObject.h"

APlacedObject::APlacedObject()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;
    Root->SetMobility(EComponentMobility::Movable);     // 나중에 끌어서 옮기므로
}