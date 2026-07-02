// WeedUiAuthoring — editor-only helper om WidgetBlueprint-trees programmatisch op te bouwen vanuit
// Python (UnrealClaude). Python kan de WidgetTree NIET aanraken (protected); C++ wel. Eén functie
// BuildTree() neemt een JSON-beschrijving en bouwt 'm in de WBP. Daarna Python: compile_blueprint +
// save_loaded_asset. Kern-tool voor de UI-rework (zie de ui-rework-plan memory).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WeedUiAuthoring.generated.h"

#if WITH_EDITOR

class UWidgetBlueprint;

UCLASS()
class WEEDSHOPCORE_API UWeedUiAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Bouw (vervang) de widget-tree van WBP uit een JSON-spec.
	// Node = { "type": "...", "name": "...", "props": { ... }, "slot": { ... }, "children": [ ... ] }.
	// type: CanvasPanel | VerticalBox | HorizontalBox | Overlay | SizeBox | Border | ScrollBox |
	//       TextBlock | Image | Button | ProgressBar.
	// props (afhankelijk van type): text, fontSize, fontBold, textColor "r,g,b,a", justify(0=L,1=C,2=R),
	//       brushColor "r,g,b,a", radius, padding "l,t,r,b", widthOverride, heightOverride,
	//       texture "/Game/...", tint "r,g,b,a", percent, fillColor "r,g,b,a".
	// slot: canvas -> anchorMin "x,y", anchorMax "x,y", pos "x,y", size "x,y", align "x,y", autoSize,
	//       offsets "l,t,r,b";  box -> padding "l,t,r,b", hAlign(0..3), vAlign(0..3), fill(0..1).
	// Geeft true bij succes. JsonSpec is één root-node-object.
	// Param is UObject* (niet UWidgetBlueprint*): UHT kan het editor-type niet resolven in een
	// SHIPPING-target (UMGEditor zit daar niet in de module-graph) -> packagen brak. Binnenin gecast;
	// voor de Python-aanroep (unreal.WeedUiAuthoring.build_tree(wbp, json)) verandert er niets.
	UFUNCTION(BlueprintCallable, Category = "WeedUiAuthoring")
	static bool BuildTree(UObject* WBP, const FString& JsonSpec);
};

#endif // WITH_EDITOR
