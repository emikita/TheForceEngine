#pragma once

#include "TFE_System/types.h"
#include "TFE_RenderBackend/textureGpu.h"

namespace vr
{
	enum Gfx
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
		//Vec3f	mPosition;
		Mat4	mTransformation;// { ae::core::mat4::GetIdentity() };
		//quat	mRotationLocal;
		//Vec3f	mPositionLocal;
		//Mat4	mTransformationLocal;// { ae::core::mat4::GetIdentity() };
		Vec3f	mVelocity;			// velocity in tracker space in m/s
		Vec3f	mAngularVelocity;	// angular velocity in radians/s (?)
		bool	mIsValid{ false };
	};

	enum ControllerButtons
	{
		A = 1 << 0, // X
		B = 1 << 1, // Y
		Menu = 1 << 2,
		Thumb = 1 << 3,
		Shoulder = 1 << 4,
	};

	enum Feature
	{
		EyeTracking = 1 << 0,
		Passthrough = 1 << 1
	};

	enum class UpdateStatus
	{
		Ok,
		ShouldQuit,
		ShouldDestroy,
		ShouldNotRender,
		NotVisible
	};

	struct ControllerState
	{
		uint32_t	mControllerButtons{ 0 };
		float		mHandTrigger{ 0.0f };
		float		mIndexTrigger{ 0.0f };
		Vec2f		mThumbStick{ 0.0f, 0.0f };
		Vec2f		mTrackpad{ 0.0f, 0.0f };
	};

	struct HapticVibration
	{
		float mDuration;	// seconds, < 0.0f = minimal
		float mFrequency;	// Hz, 0.0f = unspecified
		float mAmplitude;	// [0.0f, 1.0f]
	};

	bool Initialize(Gfx gfx);
	bool IsInitialized();
	void Deinitialize();

	void ClearRenderTarget(Side eye);
	//const Vec2ui& GetTargetSize() const{ return mTargetSize; }
	bool UseMultiView();
	bool IsFeatureSupported(Feature feature);

	UpdateStatus UpdateFrame(const Mat3& cameraRotOrig, const Vec3f& cameraPosOrig, const Mat4& projOrig, float userScale);
	void UpdateView(Side eye);
	void Commit(Side eye);
	bool SubmitFrame();

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

	//bool CreateSwapChain(const Vec2ui& size);
	//size_t GetTextureSwapChainLength();
	//void DestroySwapChain();
}
