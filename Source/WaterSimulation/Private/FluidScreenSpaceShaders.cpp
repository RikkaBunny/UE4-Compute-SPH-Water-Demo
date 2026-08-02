#include "FluidScreenSpaceShaders.h"

IMPLEMENT_GLOBAL_SHADER(
	FSPHScreenSplatCS,
	"/WaterSimulation/Private/FluidScreenSpace.usf",
	"ScreenSplatCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FSPHShadowSplatCS,
	"/WaterSimulation/Private/FluidScreenSpace.usf",
	"ShadowSplatCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FSPHShadowSmoothCS,
	"/WaterSimulation/Private/FluidScreenSpace.usf",
	"ShadowSmoothCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FSPHDecodeSmoothDepthCS,
	"/WaterSimulation/Private/FluidScreenSpace.usf",
	"DecodeSmoothDepthCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FSPHSmoothDepthCS,
	"/WaterSimulation/Private/FluidScreenSpace.usf",
	"SmoothDepthCS",
	SF_Compute);

IMPLEMENT_GLOBAL_SHADER(
	FSPHScreenCompositePS,
	"/WaterSimulation/Private/FluidScreenSpace.usf",
	"ScreenCompositePS",
	SF_Pixel);
