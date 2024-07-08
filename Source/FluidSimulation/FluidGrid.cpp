#include "FluidGrid.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "RHI.h"
#include "RenderGraphResources.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"

AFluidGrid::AFluidGrid()
{
	PrimaryActorTick.bCanEverTick = true;

	int32 TotalSize = Size * Size;
	Density.SetNumZeroed(TotalSize);
	Vx.SetNumZeroed(TotalSize);
	Vy.SetNumZeroed(TotalSize);
	Vz.SetNumZeroed(TotalSize);

	PlaneComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaneComponent"));
	RootComponent = PlaneComponent;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshAsset(TEXT("/Game/Path/To/PlaneMesh"));
	if (PlaneMeshAsset.Succeeded())
	{
		PlaneComponent->SetStaticMesh(PlaneMeshAsset.Object);
		PlaneComponent->SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));
	}
}

void AFluidGrid::InitializeRenderTarget()
{
	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitAutoFormat(Size, Size);
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	RenderTarget->AddToRoot();
	RenderTarget->UpdateResource();
}

void AFluidGrid::BeginPlay()
{
	Super::BeginPlay();

	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("BaseMaterial is not set."));
		return;
	}

	InitializeRenderTarget();

	DynamicMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (DynamicMaterialInstance)
	{
		DynamicMaterialInstance->SetTextureParameterValue(FName("DynamicTexture"), RenderTarget);
		PlaneComponent->SetMaterial(0, DynamicMaterialInstance);
	}
}

void AFluidGrid::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	StepSimulation();
	UpdateRenderTarget();

	HandleInput();
}

void AFluidGrid::HandleInput()
{
	if (GetWorld()->GetFirstPlayerController()->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		FVector2D MousePosition;
		GetWorld()->GetFirstPlayerController()->GetMousePosition(MousePosition.X, MousePosition.Y);

		FVector WorldPosition, WorldDirection;
		GetWorld()->GetFirstPlayerController()->DeprojectScreenPositionToWorld(MousePosition.X, MousePosition.Y, WorldPosition, WorldDirection);

		int32 GridX = FMath::Clamp(static_cast<int32>(WorldPosition.X), 0, Size - 1);
		int32 GridY = FMath::Clamp(static_cast<int32>(WorldPosition.Y), 0, Size - 1);

		AddDensity(GridX, GridY, 100.0f);
	}
}

void AFluidGrid::UpdateRenderTarget()
{
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("RenderTarget is not initialized."));
		return;
	}

	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	TArray<FLinearColor> ColorData;
	ColorData.SetNum(Size * Size);

	for (int32 y = 0; y < Size; y++)
	{
		for (int32 x = 0; x < Size; x++)
		{
			float Value = Density[IX(x, y)];
			float Intensity = FMath::Clamp(Value, 0.0f, 1.0f);

			FLinearColor Color;
			Color.R = Intensity;
			Color.G = 0.0f;
			Color.B = 1.0f - Intensity;
			Color.A = 1.0f;

			ColorData[IX(x, y)] = Color;
		}
	}

	int32 LocalSize = Size;
	ENQUEUE_RENDER_COMMAND(UpdateRenderTarget)(
		[RenderTargetResource, ColorData, LocalSize](FRHICommandListImmediate& RHICmdList)
		{
			FUpdateTextureRegion2D UpdateRegion(0, 0, 0, 0, LocalSize, LocalSize);
			int32 Pitch = LocalSize * sizeof(FLinearColor);
			RHICmdList.UpdateTexture2D(
				RenderTargetResource->GetRenderTargetTexture(), 0, UpdateRegion, Pitch, (uint8*)ColorData.GetData()
			);
		}
		);

	UE_LOG(LogTemp, Log, TEXT("Updated render target with new density values."));
}

void AFluidGrid::AddDensity(int32 x, int32 y, float amount)
{
	Density[IX(x, y)] += amount;
	UE_LOG(LogTemp, Log, TEXT("Added Density at (%d, %d) = %f"), x, y, Density[IX(x, y)]);
}

void AFluidGrid::AddVelocity(int32 x, int32 y, float amountX, float amountY)
{
	int32 index = IX(x, y);
	Vx[index] += amountX;
	Vy[index] += amountY;
}

void AFluidGrid::StepSimulation()
{
	UE_LOG(LogTemp, Log, TEXT("Starting StepSimulation"));

	AddDensity(Size / 2, Size / 2, 100.0f);
	AddVelocity(Size / 2, Size / 2, 1.0f, 0.0f);

	UE_LOG(LogTemp, Log, TEXT("Added initial density and velocity."));

	TArray<float> Vx0 = Vx;
	TArray<float> Vy0 = Vy;
	TArray<float> Density0 = Density;

	UE_LOG(LogTemp, Log, TEXT("Initial Density at center = %f"), Density[IX(Size / 2, Size / 2)]);

	Diffuse(1, Vx, Vx0, Viscosity, Dt);
	Diffuse(2, Vy, Vy0, Viscosity, Dt);
	UE_LOG(LogTemp, Log, TEXT("Diffused velocities."));

	Project(Vx, Vy, Vx0, Vy0);
	UE_LOG(LogTemp, Log, TEXT("Projected velocities."));

	Advect(1, Vx, Vx0, Vx0, Vy0, Dt);
	Advect(2, Vy, Vy0, Vx0, Vy0, Dt);
	UE_LOG(LogTemp, Log, TEXT("Advected velocities."));

	Project(Vx, Vy, Vx0, Vy0);
	UE_LOG(LogTemp, Log, TEXT("Projected velocities again."));

	Diffuse(0, Density, Density0, Diffusion, Dt);
	UE_LOG(LogTemp, Log, TEXT("Density after Diffusion at center = %f"), Density[IX(Size / 2, Size / 2)]);

	Advect(0, Density, Density0, Vx, Vy, Dt);
	UE_LOG(LogTemp, Log, TEXT("Final Density at center = %f"), Density[IX(Size / 2, Size / 2)]);

	UE_LOG(LogTemp, Log, TEXT("Advected density."));

	UpdateRenderTarget();
	UE_LOG(LogTemp, Log, TEXT("Updated render target."));
}

void AFluidGrid::Diffuse(int32 b, TArray<float>& x, TArray<float>& x0, float diff, float dt)
{
	float a = dt * diff * (Size - 2) * (Size - 2);
	UE_LOG(LogTemp, Log, TEXT("Diffuse: a = %f, diff = %f, dt = %f"), a, diff, dt);
	LinearSolve(b, x, x0, a, 1 + 4 * a);

	for (int32 i = 0; i < FMath::Min(Size, 10); i++)
	{
		UE_LOG(LogTemp, Log, TEXT("Diffuse: Density[%d] = %f"), i, x[i]);
	}
}

void AFluidGrid::Advect(int32 b, TArray<float>& d, TArray<float>& d0, TArray<float>& velocX, TArray<float>& velocY, float dt)
{
	float dtx = dt * (Size - 2);
	float dty = dt * (Size - 2);
	float Nfloat = Size - 2;
	int32 i, j;

	for (j = 1; j < Size - 1; j++)
	{
		for (i = 1; i < Size - 1; i++)
		{
			float x = i - dtx * velocX[IX(i, j)];
			float y = j - dty * velocY[IX(i, j)];

			x = FMath::Clamp(x, 0.5f, Nfloat + 0.5f);
			y = FMath::Clamp(y, 0.5f, Nfloat + 0.5f);

			int32 i0 = FMath::FloorToInt(x);
			int32 i1 = i0 + 1;
			int32 j0 = FMath::FloorToInt(y);
			int32 j1 = j0 + 1;

			float s1 = x - i0;
			float s0 = 1.0f - s1;
			float t1 = y - j0;
			float t0 = 1.0f - t1;

			d[IX(i, j)] = s0 * (t0 * d0[IX(i0, j0)] + t1 * d0[IX(i0, j1)]) + s1 * (t0 * d0[IX(i1, j0)] + t1 * d0[IX(i1, j1)]);
		}
	}

	SetBoundary(b, d);
}

void AFluidGrid::Project(TArray<float>& velocX, TArray<float>& velocY, TArray<float>& p, TArray<float>& div)
{
	for (int32 j = 1; j < Size - 1; j++)
	{
		for (int32 i = 1; i < Size - 1; i++)
		{
			div[IX(i, j)] = (-0.5f * (velocX[IX(i + 1, j)] - velocX[IX(i - 1, j)] + velocY[IX(i, j + 1)] - velocY[IX(i, j - 1)])) / Size;
			p[IX(i, j)] = 0;
		}
	}

	SetBoundary(0, div);
	SetBoundary(0, p);
	LinearSolve(0, p, div, 1, 6);

	for (int32 j = 1; j < Size - 1; j++)
	{
		for (int32 i = 1; i < Size - 1; i++)
		{
			velocX[IX(i, j)] -= 0.5f * (p[IX(i + 1, j)] - p[IX(i - 1, j)]) * Size;
			velocY[IX(i, j)] -= 0.5f * (p[IX(i, j + 1)] - p[IX(i, j - 1)]) * Size;
		}
	}

	SetBoundary(1, velocX);
	SetBoundary(2, velocY);
}

void AFluidGrid::LinearSolve(int32 b, TArray<float>& x, TArray<float>& x0, float a, float c)
{
	float cRecip = 1.0f / c;
	for (int32 t = 0; t < Size; t++)
	{
		for (int32 j = 1; j < Size - 1; j++)
		{
			for (int32 i = 1; i < Size - 1; i++)
			{
				x[IX(i, j)] = (x0[IX(i, j)] + a * (x[IX(i + 1, j)] + x[IX(i - 1, j)] + x[IX(i, j + 1)] + x[IX(i, j - 1)])) * cRecip;
			}
		}
		SetBoundary(b, x);
	}

	for (int32 i = 0; i < FMath::Min(Size, 10); i++)
	{
		UE_LOG(LogTemp, Log, TEXT("LinearSolve: x[%d] = %f"), i, x[i]);
	}
}

void AFluidGrid::SetBoundary(int32 b, TArray<float>& x)
{
	for (int32 i = 1; i < Size - 1; i++)
	{
		x[IX(i, 0)] = b == 2 ? -x[IX(i, 1)] : x[IX(i, 1)];
		x[IX(i, Size - 1)] = b == 2 ? -x[IX(i, Size - 2)] : x[IX(i, Size - 2)];
	}
	for (int32 j = 1; j < Size - 1; j++)
	{
		x[IX(0, j)] = b == 1 ? -x[IX(1, j)] : x[IX(1, j)];
		x[IX(Size - 1, j)] = b == 1 ? -x[IX(1, j)] : x[IX(Size - 2, j)];
	}

	x[IX(0, 0)] = 0.5f * (x[IX(1, 0)] + x[IX(0, 1)]);
	x[IX(0, Size - 1)] = 0.5f * (x[IX(1, Size - 1)] + x[IX(0, Size - 2)]);
	x[IX(Size - 1, 0)] = 0.5f * (x[IX(Size - 2, 0)] + x[IX(Size - 1, 1)]);
	x[IX(Size - 1, Size - 1)] = 0.5f * (x[IX(Size - 2, Size - 1)] + x[IX(Size - 1, Size - 2)]);
}
