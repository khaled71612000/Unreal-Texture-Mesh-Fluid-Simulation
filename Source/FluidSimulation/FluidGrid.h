#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/BoxComponent.h"
#include "FluidGrid.generated.h"

UCLASS()
class FLUIDSIMULATION_API AFluidGrid : public AActor
{
	GENERATED_BODY()

public:
	AFluidGrid();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	int32 Size = 256;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	int32 AreaSize = 75;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float AffectedDensity = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float AffectedVelocity = 31.0f;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float Dt = 10.1f;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float Diffusion = 1;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float Viscosity = 1;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float TurbulenceScale = 5;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float TurbulenceSpeed = 1;

	TArray<float> Density, Vx, Vy, Vz;

	UPROPERTY(VisibleAnywhere)
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(VisibleAnywhere)
	UMaterialInstanceDynamic* DynamicMaterialInstance;

	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent* PlaneComponent;

	UPROPERTY(VisibleAnywhere)
	UBoxComponent* CollisionBox;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	UMaterial* BaseMaterial;

	UPROPERTY(EditAnywhere, Category = "Fluid Simulation")
	float Scale = 150;

	void InitializeRenderTarget();
	void UpdateRenderTarget();
	void HandleInput();
	void LineTraceAndColor();

	void AddDensity(int32 x, int32 y, float amount);
	void AddVelocity(int32 x, int32 y, float amountX, float amountY);
	void Diffuse(int32 b, TArray<float>& x, TArray<float>& x0, float diff, float dt);
	void Advect(int32 b, TArray<float>& d, TArray<float>& d0, TArray<float>& velocX, TArray<float>& velocY, float dt);
	void Project(TArray<float>& velocX, TArray<float>& velocY, TArray<float>& p, TArray<float>& div);
	void LinearSolve(int32 b, TArray<float>& x, TArray<float>& x0, float a, float c);
	void SetBoundary(int32 b, TArray<float>& x);

	int32 IX(int32 x, int32 y) const;
	void StepSimulation();
	void AddRandomCentralVelocity(float magnitude);

	void RenderDensity();
	void RenderVelocity();
	void FadeDensity();
	FColor GetSmoothGradientColor(float Intensity);
};
