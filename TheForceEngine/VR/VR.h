#pragma once

#include "TFE_System/types.h"
#include "TFE_RenderBackend/textureGpu.h"

namespace vr
{
	enum class Gfx : uint32_t
	{
		OpenGL,
		Vulkan
	};

	enum Side
	{
		Left = 0,
		Right,
		Count
	};

	struct Pose
	{
		//quat	mRotation;
		Vec3f	mPosition;
		Mat4	mTransformation;// { ae::core::mat4::GetIdentity() };
		//quat	mRotationLocal;
		Vec3f	mPositionLocal;
		Mat4	mTransformationLocal;// { ae::core::mat4::GetIdentity() };
		Vec3f	mVelocity;			// velocity in tracker space in m/s
		Vec3f	mAngularVelocity;	// angular velocity in radians/s (?)
		bool	mIsValid{ false };
	};

	enum class ControllerButtons : uint32_t
	{
		A = 1 << 0, // X
		B = 1 << 1, // Y
		Menu = 1 << 2,
		Thumb = 1 << 3,
		Shoulder = 1 << 4,
	};

	enum class Feature : uint32_t
	{
		EyeTracking = 1 << 0,
		Passthrough = 1 << 1
	};

	struct ControllerState
	{
		uint32_t	mControllerButtons{ 0 };
		float		mHandTrigger{ 0.0f };
		float		mIndexTrigger{ 0.0f };
		Vec2f		mThumbStick{ 0.0f, 0.0f };
		Vec2f		mTrackpad{ 0.0f, 0.0f };
	};


	bool Initialize(Gfx gfx);
	bool IsInitialized();
	void Deinitialize();
	//const Vec2ui& GetTargetSize() const{ return mTargetSize; }
	//bool UseMultiView() const;
	bool IsFeatureSupported(Feature feature);

	//UpdateStatus UpdateFrame(const Camera& camera, float userScale);
	//bool UpdateView(uint32_t viewIndex);
	//bool SubmitFrame();

	//const Mat4& GetEyeProj(int eye);
	//const Mat4& GetEyeLtw(int eye);
	//const Pose& GetEyePose(int eye);

	//const Pose& GetHandPose(int hand);
	//const ControllerState& GetControllerState(int hand);

	//const Pose& GetEyeGazePose();

	//void StartPassthrough();
	//void StopPassthrough();
	//bool IsPassthroughEnabled();

	//RenderTarget* GetCurrentTarget(int eye);
	//void Commit(int eye);

	bool CreateTextureSwapChain(const Vec2ui& size);
	int GetTextureSwapChainLength();
	void DestroySwapChain();
}
