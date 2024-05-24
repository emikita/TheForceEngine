#include "VR.h"
#include "OpenGLDebug.h"
#include <TFE_System/system.h>
#include <glew/include/GL/glew.h>
#include <vector>
#include <array>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#define NOMINMAX
#define _ENABLE_EXTENDED_ALIGNED_STORAGE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct IUnknown;
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#elif defined(ANDROID)
#define USE_GL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_PLATFORM_ANDROID
#else
#error "Specify OpenXR platform & graphics API"
#endif

#if defined(XR_USE_GRAPHICS_API_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include "openxr/openxr_platform.h"

#include "OpenXRDebug.h"
#include "openxr/common/xr_linear.h"

#if !defined(_WIN32)
#define strcpy_s(dest, source) strncpy((dest), (source), sizeof(dest))
#endif

namespace
{
//namespace vr
//{
	bool												mInitialized{ false };
	vr::Gfx												mGfx;
	bool												mEyeTrackingSupported{ false };
	bool												mPassthroughSupported{ false };
	bool												mHeadsetIdDetectionSupported{ false };
	bool												mHeadsetIdDetectionSupportedMeta{ false };
	Vec3ui			 									mTargetSize{ 0, 0 };
	uint32_t											mSwapChainLength{ 0 };
	GLenum												mSwapchainFormat;
	const std::vector<GLenum>							mRequestedSwapchainFormats =
	{
		GL_RGB10_A2,
		GL_RGBA16F,
		// The two below should only be used as a fallback, as they are linear color formats without enough bits for color
		// depth, thus leading to banding.
		GL_RGBA8,
		GL_RGBA8_SNORM,
	};
	bool												mExitRenderLoop{ false };
	bool												mRequestRestart{ false };
	bool												mSessionRunning{ false };

namespace openxr
{
	using namespace vr;
	using namespace vr::openxr::detail;

	XrInstance											mInstance{ XR_NULL_HANDLE };
	XrDebugUtilsMessengerEXT							mMessenger{ XR_NULL_HANDLE };
	XrSession											mSession{ XR_NULL_HANDLE };
	XrSpace												mAppSpace{ XR_NULL_HANDLE };

	XrPassthroughFB										mPassthrough{ XR_NULL_HANDLE };
	XrPassthroughLayerFB								mPassthroughLayer{ XR_NULL_HANDLE };

	std::vector<XrViewConfigurationView>				mViewConfigViews;
	std::vector<XrCompositionLayerProjectionView>		mProjectionLayerViews;
	XrFrameState										mFrameState{ XR_TYPE_FRAME_STATE };

	std::array<Pose, Side::Count>						mHandPose;
	std::array<ControllerState, Side::Count>			mControllerState;

	Pose												mEyeGazePose;

	std::array<Mat4, Side::Count>						mProj;
	std::array<Pose, Side::Count>						mEyePose;

	float												mUserScale;

	std::array<XrSwapchain, Side::Count>				mSwapchains{ nullptr };
	//std::array<std::vector<Texture>, 2>				mColorTextures;
	//std::array<std::vector<Texture>, 2>				mDepthTextures;
	//std::array<std::vector<RenderTarget>, 2>			mRenderTargets;
	uint32_t											mTextureSwapChainIndex{ 0 };

#define CHECK_XR_RESULT(x)										\
	{															\
		XrResult xr_result;										\
		if (XR_FAILED(xr_result = x))							\
		{														\
			TFE_ERROR("VR", "XR error: {}", GetResultStr(xr_result));	\
		}														\
	}

#define CHECK_XR_RESULT2(x)												\
	{																	\
		XrResult res;													\
		if (XR_FAILED(res = x))											\
		{																\
			TFE_ERROR("VR", "{} returns XR error: {}", #x, GetResultStr(res));	\
		}																\
		else															\
		{																\
			TFE_INFO("VR", "{} passed", #x);							\
		}																\
	}

#define DECLARE_XR_PFN(pfn) PFN_##pfn pfn = nullptr

#define LOAD_XR_FUNCTION(res, instance, function)														\
	if (res == XR_SUCCESS)																				\
	{																									\
		res = xrGetInstanceProcAddr(instance, #function, (PFN_xrVoidFunction*)&function);				\
	}																									\
	else 																								\
	{																									\
		TFE_ERROR("VR", "failed to load function pointer for {} with error {}", #function, GetResultStr(res));	\
	}

	// XR_EXT_debug_utils
	DECLARE_XR_PFN(xrCreateDebugUtilsMessengerEXT);
	DECLARE_XR_PFN(xrDestroyDebugUtilsMessengerEXT);


#if defined(USE_GL_ES)
	// XR_KHR_opengl_es_enable
	DECLARE_XR_PFN(xrGetOpenGLESGraphicsRequirementsKHR);
#else
	// XR_KHR_opengl_enable
	DECLARE_XR_PFN(xrGetOpenGLGraphicsRequirementsKHR);
#endif

#if defined(XR_USE_GRAPHICS_API_VULKAN)
	// XR_KHR_vulkan_enable/XR_KHR_vulkan_enable2
	DECLARE_XR_PFN(xrGetVulkanGraphicsRequirementsKHR);
#endif

	// XR_FB_passthrough
	DECLARE_XR_PFN(xrCreatePassthroughFB);
	DECLARE_XR_PFN(xrDestroyPassthroughFB);
	DECLARE_XR_PFN(xrPassthroughStartFB);
	DECLARE_XR_PFN(xrPassthroughPauseFB);
	DECLARE_XR_PFN(xrCreatePassthroughLayerFB);
	DECLARE_XR_PFN(xrDestroyPassthroughLayerFB);
	DECLARE_XR_PFN(xrPassthroughLayerPauseFB);
	DECLARE_XR_PFN(xrPassthroughLayerResumeFB);
	DECLARE_XR_PFN(xrPassthroughLayerSetStyleFB);
	DECLARE_XR_PFN(xrCreateGeometryInstanceFB);
	DECLARE_XR_PFN(xrDestroyGeometryInstanceFB);
	DECLARE_XR_PFN(xrGeometryInstanceSetTransformFB);

	// XR_FB_triangle_mesh
	DECLARE_XR_PFN(xrCreateTriangleMeshFB);
	DECLARE_XR_PFN(xrDestroyTriangleMeshFB);
	DECLARE_XR_PFN(xrTriangleMeshGetVertexBufferFB);
	DECLARE_XR_PFN(xrTriangleMeshGetIndexBufferFB);
	DECLARE_XR_PFN(xrTriangleMeshBeginUpdateFB);
	DECLARE_XR_PFN(xrTriangleMeshEndUpdateFB);
	DECLARE_XR_PFN(xrTriangleMeshBeginVertexBufferUpdateFB);
	DECLARE_XR_PFN(xrTriangleMeshEndVertexBufferUpdateFB);

	namespace DebugUtilsEXT
	{
		using MessageSeverityFlagBits = XrDebugUtilsMessageSeverityFlagsEXT;
		using MessageTypeFlagBits = XrDebugUtilsMessageTypeFlagsEXT;
		using MessageSeverityFlags = XrDebugUtilsMessageSeverityFlagsEXT;
		constexpr MessageSeverityFlags ALL_SEVERITIES =
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		using MessageTypeFlags = XrDebugUtilsMessageTypeFlagsEXT;
		constexpr MessageTypeFlags ALL_TYPES =
			XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
		using CallbackData = XrDebugUtilsMessengerCallbackDataEXT;
		using Messenger = XrDebugUtilsMessengerEXT;

		bool enabled{ false };

		// Raw C callback
		static XrBool32 DebugCallback(XrDebugUtilsMessageSeverityFlagsEXT severity,
			XrDebugUtilsMessageTypeFlagsEXT type,
			const XrDebugUtilsMessengerCallbackDataEXT* data,
			void* userData)
		{
			const char* functionName = data->functionName ? data->functionName : "no function";
			const char* message = data->message ? data->message : "no message";
			switch (severity)
			{
			case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				TFE_INFO("VR", "{}: {}", functionName, message);
				break;
			case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				TFE_INFO("VR", "{}: {}", functionName, message);
				break;
			case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				TFE_WARN("VR", "{}: {}", functionName, message);
				break;
			case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				TFE_ERROR("VR", "{}: {}", functionName, message);
				break;
			default:
				TFE_WARN("VR", "missing OpenXR severity handler");
				TFE_WARN("VR", "{}: {}", functionName, message);
				break;
			}

			return XR_FALSE;
		}

		Messenger Create(const XrInstance& instance,
			const MessageSeverityFlags& severityFlags = ALL_SEVERITIES,
			const MessageTypeFlags& typeFlags = ALL_TYPES,
			void* userData = nullptr)
		{
			XrResult res = XR_SUCCESS;
			LOAD_XR_FUNCTION(res, instance, xrCreateDebugUtilsMessengerEXT);
			LOAD_XR_FUNCTION(res, instance, xrDestroyDebugUtilsMessengerEXT);
			if (XR_FAILED(res))
			{
				TFE_ERROR("VR", "failed to load OpenXR debug utils extension functions, extension disabled");
				enabled = false;
				return XR_NULL_HANDLE;
			}

			XrDebugUtilsMessengerCreateInfoEXT createInfo{ XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, nullptr, severityFlags, typeFlags, DebugCallback, userData };
			Messenger result;
			CHECK_XR_RESULT2(xrCreateDebugUtilsMessengerEXT(instance, &createInfo, &result));
			return result;
		}
	} // namespace DebugUtilsEXT

	namespace Math {
		namespace Pose {
			XrPosef Identity() {
				XrPosef t{};
				t.orientation.w = 1;
				return t;
			}

			//XrPosef Translation(const XrVector3f& translation) {
			//	XrPosef t = Identity();
			//	t.position = translation;
			//	return t;
			//}

			//XrPosef RotateCCWAboutYAxis(float radians, XrVector3f translation) {
			//	XrPosef t = Identity();
			//	t.orientation.x = 0.f;
			//	t.orientation.y = std::sin(radians * 0.5f);
			//	t.orientation.z = 0.f;
			//	t.orientation.w = std::cos(radians * 0.5f);
			//	t.position = translation;
			//	return t;
			//}
		} // namespace Pose
	} // namespace Math

	// Description of one of the spaces we want to render in, along with a scale factor to
	// be applied in that space. In the original example, this is used to position, orient,
	// and scale cubes to various spaces including hand space.
	struct Space
	{
		XrPosef Pose;		///< Pose of the space relative to g_appSpace
		XrVector3f Scale;	///< Scale hint for the space
		std::string Name;	///< An identifier so we can know what to render in each space
	};

	//// Maps from the space back to its name so we can know what to render in each
	//std::map<XrSpace, std::string> g_spaceNames;

	//inline bool EqualsIgnoreCase(const std::string& s1, const std::string& s2, const std::locale& loc = std::locale()) {
	//	const std::ctype<char>& ctype = std::use_facet<std::ctype<char>>(loc);
	//	const auto compareCharLower = [&](char c1, char c2) { return ctype.tolower(c1) == ctype.tolower(c2); };
	//	return s1.size() == s2.size() && std::equal(s1.begin(), s1.end(), s2.begin(), compareCharLower);
	//}

	//XrReferenceSpaceCreateInfo GetXrReferenceSpaceCreateInfo(const std::string& referenceSpaceTypeStr)
	//{
	//	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	//	referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Identity();
	//	if (EqualsIgnoreCase(referenceSpaceTypeStr, "View")) {
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ViewFront")) {
	//		// Render head-locked 2m in front of device.
	//		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Translation({ 0.f, 0.f, -2.f }),
	//			referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Local")) {
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Stage")) {
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeft")) {
	//		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, { -2.f, 0.f, -2.f });
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
	//		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, { 2.f, 0.f, -2.f });
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
	//		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(3.14f / 3.f, { -2.f, 0.5f, -2.f });
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	//	}
	//	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
	//		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(-3.14f / 3.f, { 2.f, 0.5f, -2.f });
	//		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	//	}
	//	else {
	//		throw std::invalid_argument(Format("Unknown reference space type '{}'", referenceSpaceTypeStr));
	//	}
	//	return referenceSpaceCreateInfo;
	//}

	const XrViewConfigurationType viewConfigType{ XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO };

	struct InputState
	{
		XrActionSet actionSet{ XR_NULL_HANDLE };
		XrAction grabAction{ XR_NULL_HANDLE };
		XrAction poseAction{ XR_NULL_HANDLE };
		XrAction eyeGazeAction{ XR_NULL_HANDLE };
		XrAction vibrateAction{ XR_NULL_HANDLE };
		XrAction moveAction{ XR_NULL_HANDLE };
		XrAction rotateAction{ XR_NULL_HANDLE };
		XrAction gripActionLeft{ XR_NULL_HANDLE };
		XrAction gripActionRight{ XR_NULL_HANDLE };
		XrAction aActionLeft{ XR_NULL_HANDLE };
		XrAction aActionRight{ XR_NULL_HANDLE };
		XrAction bActionLeft{ XR_NULL_HANDLE };
		XrAction bActionRight{ XR_NULL_HANDLE };
		XrAction menuActionLeft{ XR_NULL_HANDLE };
		XrAction menuActionRight{ XR_NULL_HANDLE };
		XrAction shoulderActionLeft{ XR_NULL_HANDLE };
		XrAction shoulderActionRight{ XR_NULL_HANDLE };
		XrAction trackpadActionLeft{ XR_NULL_HANDLE };
		XrAction trackpadActionRight{ XR_NULL_HANDLE };
		XrAction thumbStickClickActionLeft{ XR_NULL_HANDLE };
		XrAction thumbStickClickActionRight{ XR_NULL_HANDLE };
		XrAction quitAction{ XR_NULL_HANDLE };
		std::array<XrPath, 2> handSubactionPath;
		std::array<XrSpace, 2> handSpace;
		std::array<float, 2> handScale = { {1.0f, 1.0f} };
		std::array<XrBool32, 2> handActive;
		XrSpace eyeGazeSpace;
		XrBool32 eyeGazeActive;
	};

	bool PrepareXrInstance()
	{
#if defined(ANDROID)
		{	// loader
			PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
			XrResult res = XR_SUCCESS;
			LOAD_XR_FUNCTION(res, XR_NULL_HANDLE, xrInitializeLoaderKHR);
			if (xrInitializeLoaderKHR != nullptr)
			{
				jobject activity = ae::core::android::GetActivity();
				JNIEnv* env = ae::core::android::GetJNIEnv();
				JavaVM* jvm;
				env->GetJavaVM(&jvm);

				XrLoaderInitInfoAndroidKHR loaderInitializeInfoAndroid{ XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
				loaderInitializeInfoAndroid.applicationVM = jvm;
				loaderInitializeInfoAndroid.applicationContext = activity;
				xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&loaderInitializeInfoAndroid);
			}
		}
#endif // defined(ANDROID)

		// get supported extension list
		uint32_t extensionCount{ 0 };
		CHECK_XR_RESULT(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr));
		std::vector<XrExtensionProperties> extensions;
		extensions.resize(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
		CHECK_XR_RESULT(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data()));
		std::unordered_map<std::string, XrExtensionProperties> supportedExtensions;
		TFE_INFO("VR", "OpenXR extensions({}):", extensionCount);
		for (const auto& extensionProperties : extensions)
		{
			supportedExtensions.insert({ extensionProperties.extensionName, extensionProperties });
			TFE_INFO("VR", extensionProperties.extensionName);
		}

		std::vector<const char*> requestedExtensions;
		// add a specific extension to the list of extensions to be enabled, if it is supported
		auto EnableExtensionIfSupported = [&](const char* extensionName) {
			for (size_t i = 0; i < supportedExtensions.size(); i++)
			{
				if (supportedExtensions.count(extensionName) > 0)
				{
					requestedExtensions.push_back(extensionName);
					return true;
				}
			}
			return false;
		};

#if defined(ANDROID)
		if (!EnableExtensionIfSupported(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME))
		{
			TFE_ERROR("VR", "OpenXR '{}' extension is not supported.", XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
			return false;
		}
#endif

		if (mGfx == vr::Gfx::OpenGL)
		{
#if defined(USE_GL_ES)
			if (!EnableExtensionIfSupported(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME))
			{
				TFE_ERROR("VR", "OpenXR '{}' extension is not supported.", XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
				return false;
			}
#else
			if (!EnableExtensionIfSupported(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
			{
				TFE_ERROR("VR", "OpenXR '{}' extension is not supported.", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
				return false;
			}
#endif
		}
#if defined(XR_USE_GRAPHICS_API_VULKAN)
		else if (mGfx == vr::Gfx::Vulkan)
		{
			if (!EnableExtensionIfSupported(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
			{
				TFE_ERROR("VR", "OpenXR '{}' extension is not supported.", XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
				return false;
			}
		}
#endif
		else
		{
			TFE_ERROR("VR", "support for adapter type {} not implemented", (uint32_t)mGfx);
		}

		const uint8_t debugLevel = 1;
		if (debugLevel > 0)
		{
			if (!EnableExtensionIfSupported(XR_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				TFE_WARN("VR", "OpenXR '{}' extension is not supported.", XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
			}
			else
			{
				DebugUtilsEXT::enabled = true;
			}
		}

		mPassthroughSupported = EnableExtensionIfSupported(XR_FB_PASSTHROUGH_EXTENSION_NAME);
		if (!mPassthroughSupported)
		{
			TFE_INFO("VR", "OpenXR '{}' extension is not supported.", XR_FB_PASSTHROUGH_EXTENSION_NAME);
		}

		mEyeTrackingSupported = false;// EnableExtensionIfSupported(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
		if (!mEyeTrackingSupported)
		{
			TFE_INFO("VR", "OpenXR '{}' extension is not supported.", XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
		}

		//if (mHeadsetIdDetectionSupported = EnableExtensionIfSupported(XR_EXT_UUID_EXTENSION_NAME); mHeadsetIdDetectionSupported)
		{
			mHeadsetIdDetectionSupportedMeta = EnableExtensionIfSupported(XR_META_HEADSET_ID_EXTENSION_NAME);
			if (!mHeadsetIdDetectionSupportedMeta)
			{
				TFE_INFO("VR", "OpenXR '{}' extension is not supported.", XR_META_HEADSET_ID_EXTENSION_NAME);
			}
		}
		//else
		//{
		//	TFE_INFO("VR", "OpenXR '{}' extension is not supported.", XR_EXT_UUID_EXTENSION_NAME);
		//}

		XrApplicationInfo appInfo{ "Dark Forces VR", 0, "TheForceEngineVR", 0, XR_CURRENT_API_VERSION };
		XrInstanceCreateInfo ici{
			XR_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, appInfo, 0, nullptr, (uint32_t)requestedExtensions.size(),
			requestedExtensions.data()
		};

#if defined(ANDROID)
		XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
		{
			jobject activity = ae::core::android::GetActivity();
			JNIEnv* env = ae::core::android::GetJNIEnv();
			JavaVM* jvm;
			env->GetJavaVM(&jvm);
			instanceCreateInfoAndroid.applicationVM = jvm;
			instanceCreateInfoAndroid.applicationActivity = activity;
			ici.next = &instanceCreateInfoAndroid;
		}
#endif

		XrDebugUtilsMessengerCreateInfoEXT dumci{ XR_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr };
		if (DebugUtilsEXT::enabled)
		{
			dumci.messageSeverities = DebugUtilsEXT::ALL_SEVERITIES;
			dumci.messageTypes = DebugUtilsEXT::ALL_TYPES;
			dumci.userData = nullptr;// this;
			dumci.userCallback = &DebugUtilsEXT::DebugCallback;
#if defined(ANDROID)
			instanceCreateInfoAndroid.next = &dumci;
#else
			ici.next = &dumci;
#endif
		}

		// create the actual instance
		CHECK_XR_RESULT2(xrCreateInstance(&ici, &mInstance));

		XrInstanceProperties instanceProperties{ XR_TYPE_INSTANCE_PROPERTIES };
		CHECK_XR_RESULT2(xrGetInstanceProperties(mInstance, &instanceProperties));
		TFE_INFO("VR", "OpenXR Runtime={} Version={}.{}.{}",
			instanceProperties.runtimeName, XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
			XR_VERSION_MINOR(instanceProperties.runtimeVersion), XR_VERSION_PATCH(instanceProperties.runtimeVersion));

		// turn on debug logging
		if (DebugUtilsEXT::enabled)
		{
			mMessenger = DebugUtilsEXT::Create(mInstance);
		}

		if (mPassthroughSupported)
		{
			XrResult res = XR_SUCCESS;
			LOAD_XR_FUNCTION(res, mInstance, xrCreatePassthroughFB);
			LOAD_XR_FUNCTION(res, mInstance, xrDestroyPassthroughFB);
			LOAD_XR_FUNCTION(res, mInstance, xrPassthroughStartFB);
			LOAD_XR_FUNCTION(res, mInstance, xrPassthroughPauseFB);
			LOAD_XR_FUNCTION(res, mInstance, xrCreatePassthroughLayerFB);
			LOAD_XR_FUNCTION(res, mInstance, xrDestroyPassthroughLayerFB);
			LOAD_XR_FUNCTION(res, mInstance, xrPassthroughLayerPauseFB);
			LOAD_XR_FUNCTION(res, mInstance, xrPassthroughLayerResumeFB);
			LOAD_XR_FUNCTION(res, mInstance, xrPassthroughLayerSetStyleFB);
			LOAD_XR_FUNCTION(res, mInstance, xrCreateGeometryInstanceFB);
			LOAD_XR_FUNCTION(res, mInstance, xrDestroyGeometryInstanceFB);
			LOAD_XR_FUNCTION(res, mInstance, xrGeometryInstanceSetTransformFB);
			if (XR_FAILED(res))
			{
				TFE_ERROR("VR", "failed to load passthrough extension functions, extension disabled");
				mPassthroughSupported = false;
			}
		}

		return true;
	}

	XrSystemId systemId;
	bool PrepareXrSystem()
	{
		XrSystemGetInfo getInfo{ XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY };
		CHECK_XR_RESULT2(xrGetSystem(mInstance, &getInfo, &systemId));

		if (mGfx == vr::Gfx::OpenGL)
		{
			XrResult res = XR_SUCCESS;
#if defined(USE_GL_ES)
			LOAD_XR_FUNCTION(res, mInstance, xrGetOpenGLESGraphicsRequirementsKHR);
			XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
			CHECK_XR_RESULT2(xrGetOpenGLESGraphicsRequirementsKHR(mInstance, systemId, &graphicsRequirements));
			TFE_INFO("VR", "OpenGL ES API supported {}.{}.{} - {}.{}.{}",
				XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported), XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported),
				XR_VERSION_PATCH(graphicsRequirements.minApiVersionSupported),
				XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported), XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported),
				XR_VERSION_PATCH(graphicsRequirements.maxApiVersionSupported));
#else
			LOAD_XR_FUNCTION(res, mInstance, xrGetOpenGLGraphicsRequirementsKHR);
			XrGraphicsRequirementsOpenGLKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
			CHECK_XR_RESULT2(xrGetOpenGLGraphicsRequirementsKHR(mInstance, systemId, &graphicsRequirements));
			TFE_INFO("VR", "OpenGL API supported {}.{}.{} - {}.{}.{}",
				XR_VERSION_MAJOR(graphicsRequirements.minApiVersionSupported), XR_VERSION_MINOR(graphicsRequirements.minApiVersionSupported),
				XR_VERSION_PATCH(graphicsRequirements.minApiVersionSupported),
				XR_VERSION_MAJOR(graphicsRequirements.maxApiVersionSupported), XR_VERSION_MINOR(graphicsRequirements.maxApiVersionSupported),
				XR_VERSION_PATCH(graphicsRequirements.maxApiVersionSupported));
#endif
		}
#if defined(XR_USE_GRAPHICS_API_VULKAN)
		else if (mGfx == vr::Gfx::Vulkan)
		{
			XrResult res = XR_SUCCESS;
			LOAD_XR_FUNCTION(res, mInstance, xrGetVulkanGraphicsRequirementsKHR);
		}
#endif
		else
		{
			TFE_ERROR("VR", "support for adapter type {} not implemented", (uint32_t)mGfx);
			return false;
		}

		return true;
	}

	bool PrepareXrSession()
	{
		auto GetGraphicsBinding = []() -> const void* {
#if defined(_WIN32)
#if defined(XR_USE_GRAPHICS_API_VULKAN)
			if (mGfx == vr::Gfx::Vulkan)
			{
				static XrGraphicsBindingVulkan2KHR graphicsBindingVl{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR, nullptr /*, TODO: openxr vulkan */ };
				return &graphicsBindingVl;
			}
#endif
			static XrGraphicsBindingOpenGLWin32KHR graphicsBindingGL{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR, nullptr, wglGetCurrentDC(), wglGetCurrentContext() };
			return &graphicsBindingGL;
#elif defined(ANDROID)
#if defined(XR_USE_GRAPHICS_API_VULKAN)
			if (mGfx == vr::Gfx::Vulkan)
			{
				static XrGraphicsBindingVulkan2KHR graphicsBindingVl{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR, nullptr /*, TODO: openxr vulkan */ };
				return &graphicsBindingVl;
			}
#endif
			static XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR, nullptr, eglGetCurrentDisplay(), (EGLConfig)0 /*TODO: not sure*/, eglGetCurrentContext() };
			return &graphicsBinding;
#else
#error "XrGraphicsBinding* not handled"
#endif
		};
		XrSessionCreateInfo createInfo{ XR_TYPE_SESSION_CREATE_INFO };
		createInfo.next = GetGraphicsBinding();
		createInfo.systemId = systemId;
		CHECK_XR_RESULT2(xrCreateSession(mInstance, &createInfo, &mSession));

		if (mPassthroughSupported)
		{
			XrPassthroughCreateInfoFB passthroughCreateInfo{ XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
			CHECK_XR_RESULT2(xrCreatePassthroughFB(mSession, &passthroughCreateInfo, &mPassthrough));
		}

		return true;
	}

	template <typename XrStruct, typename XrExtension>
	void InsertExtensionStruct(XrStruct& xrStruct, XrExtension& xrExtension)
	{
		xrExtension.next = xrStruct.next;
		xrStruct.next = &xrExtension;
	}

	GLenum GetSwapchainFormatOpenGL(XrSession session, const std::vector<GLenum>& requestedPixelFormats)
	{
		TFE_INFO("VR", "Requested swapchain formats (#{}):", requestedPixelFormats.size());
		for (const auto& format : requestedPixelFormats)
		{
			TFE_INFO("VR", " {}", gl::GetConstStr(format));
		}

		// query the runtime's preferred swapchain formats
		uint32_t swapchainFormatCount;
		CHECK_XR_RESULT(xrEnumerateSwapchainFormats(session, 0, &swapchainFormatCount, nullptr));

		std::vector<int64_t> swapchainFormats(swapchainFormatCount);
		CHECK_XR_RESULT(xrEnumerateSwapchainFormats(session, (uint32_t)swapchainFormats.size(), &swapchainFormatCount, swapchainFormats.data()));

		TFE_INFO("VR", "Supported swapchain formats (#{}):", swapchainFormats.size());
		for (const auto& formatGL : swapchainFormats)
		{
			TFE_INFO("VR", " {}", gl::GetConstStr((GLenum)formatGL));
		}

		// choose the first runtime-preferred format that this app supports
		auto SelectPixelFormat = [](const std::vector<GLenum>& applicationSupportedFormats, const std::vector<int64_t>& runtimePreferredFormats) -> GLenum {
			auto found = std::find_first_of(std::begin(applicationSupportedFormats), std::end(applicationSupportedFormats),
				std::begin(runtimePreferredFormats), std::end(runtimePreferredFormats), [](GLenum applicationSupportedFormat, int64_t runtimePreferredFormat) {
					return applicationSupportedFormat == (GLenum)runtimePreferredFormat;
				});
			if (found == std::end(applicationSupportedFormats))
			{
				return GL_NONE;
			}
			return *found;
		};

		const GLenum pixelFormat = SelectPixelFormat(requestedPixelFormats, swapchainFormats);
		TFE_INFO("VR", "Selected swapchain format={}", gl::GetConstStr(pixelFormat));

		return pixelFormat;
	}


	bool PrepareXrViewConfigViews()
	{
		{	// swapchain format
			if (mSwapchainFormat = GetSwapchainFormatOpenGL(mSession, mRequestedSwapchainFormats); mSwapchainFormat == GL_NONE)
			{
				TFE_ERROR("VR", "no compatible swapchain format found");
				return false;
			}
		}

		// log the system properties
		{
			XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };

			XrSystemEyeGazeInteractionPropertiesEXT eyeGazeInteractionProperties{ XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
			if (mEyeTrackingSupported)
			{
				InsertExtensionStruct(systemProperties, eyeGazeInteractionProperties);
			}

			XrSystemHeadsetIdPropertiesMETA headsetIdProperties{ XR_TYPE_SYSTEM_HEADSET_ID_PROPERTIES_META };
			if (mHeadsetIdDetectionSupportedMeta)
			{
				InsertExtensionStruct(systemProperties, headsetIdProperties);
			}

			CHECK_XR_RESULT2(xrGetSystemProperties(mInstance, systemId, &systemProperties));
			TFE_INFO("VR", "System Properties: Name={} VendorId={}", systemProperties.systemName, systemProperties.vendorId);
			TFE_INFO("VR", "System Graphics Properties: MaxWidth={} MaxHeight={} MaxLayers={}",
				systemProperties.graphicsProperties.maxSwapchainImageWidth,
				systemProperties.graphicsProperties.maxSwapchainImageHeight,
				systemProperties.graphicsProperties.maxLayerCount);
			TFE_INFO("VR", "System Tracking Properties: OrientationTracking={} PositionTracking={}",
				systemProperties.trackingProperties.orientationTracking == XR_TRUE ? "True" : "False",
				systemProperties.trackingProperties.positionTracking == XR_TRUE ? "True" : "False");

			if (mHeadsetIdDetectionSupportedMeta)
			{
			}

			if (mEyeTrackingSupported && !eyeGazeInteractionProperties.supportsEyeGazeInteraction)
			{
				TFE_ERROR("VR", "extension '{}' supported but system properties reports no gaze interaction", XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
				mEyeTrackingSupported = false;
			}
		}

		// TODO:
		//XrViewConfigurationProperties viewportConfig = { XR_TYPE_VIEW_CONFIGURATION_PROPERTIES };
		//OXR(xrGetViewConfigurationProperties(
		//	app.Instance, app.SystemId, viewportConfigType, &viewportConfig));
		//ALOGV(
		//	"FovMutable=%s ConfigurationType %d",
		//	viewportConfig.fovMutable ? "true" : "false",
		//	viewportConfig.viewConfigurationType);


		// find out what view configurations we have available
		{
			uint32_t viewConfigCount;
			CHECK_XR_RESULT(xrEnumerateViewConfigurations(mInstance, systemId, 0, &viewConfigCount, nullptr));
			std::vector<XrViewConfigurationType> viewConfigTypes;
			viewConfigTypes.resize(viewConfigCount);
			CHECK_XR_RESULT(xrEnumerateViewConfigurations(mInstance, systemId, viewConfigCount, &viewConfigCount, viewConfigTypes.data()));
			if (viewConfigTypes[0] != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
			{
				TFE_ERROR("VR", "only stereo-based HMD rendering supported");
				return false;
			}
		}

		// list view configuration views
		{
			uint32_t viewConfigViewsCount;
			CHECK_XR_RESULT(xrEnumerateViewConfigurationViews(mInstance, systemId, viewConfigType, 0, &viewConfigViewsCount, nullptr));
			
			mViewConfigViews.resize(viewConfigViewsCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
			CHECK_XR_RESULT(xrEnumerateViewConfigurationViews(mInstance, systemId, viewConfigType, viewConfigViewsCount, &viewConfigViewsCount, mViewConfigViews.data()));

			for (const XrViewConfigurationView& viewConfig : mViewConfigViews)
			{
				TFE_INFO("VR", "view config: recommended size [{}, {}], max size [{}, {}], recommended swapchain samples {}, max swapchain samples {}",
					viewConfig.recommendedImageRectWidth, viewConfig.recommendedImageRectHeight,
					viewConfig.maxImageRectWidth, viewConfig.maxImageRectHeight,
					viewConfig.recommendedSwapchainSampleCount, viewConfig.maxSwapchainSampleCount);
			}

			if (mViewConfigViews.size() != 2)
			{
				TFE_ERROR("VR", "unexpected number of view configurations");
				return false;
			}

			if (mViewConfigViews[0].recommendedImageRectWidth != mViewConfigViews[1].recommendedImageRectWidth ||
				mViewConfigViews[0].recommendedImageRectHeight != mViewConfigViews[1].recommendedImageRectHeight)
			{
				TFE_ERROR("VR", "per-eye images have different recommended dimensions");
				return false;
			}
		}

		mTargetSize = { mViewConfigViews[0].recommendedImageRectWidth, mViewConfigViews[0].recommendedImageRectHeight };
		//mTargetSize = { 1440, 1584 }; // Quest 2

		return true;
	}

//	//std::vector<XrSpace> g_visualizedSpaces;

	bool PrepareXrReferenceSpaces()
	{
		XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };// = GetXrReferenceSpaceCreateInfo("Local");
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Identity();
		CHECK_XR_RESULT(xrCreateReferenceSpace(mSession, &referenceSpaceCreateInfo, &mAppSpace));

		return true;
		///// @todo Change this to modify the spaces that have things drawn in them. They can all be removed
		///// if you draw things in world space. Removing these will not remove the cubes drawn for the hands.
		//std::string visualizedSpaces[] = { "ViewFront", "Local", "Stage", "StageLeft", "StageRight", "StageLeftRotated", "StageRightRotated" };

		//for (const auto& visualizedSpace : visualizedSpaces)
		//{
		//	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo(visualizedSpace);
		//	XrSpace space;
		//	XrResult res = xrCreateReferenceSpace(mSession, &referenceSpaceCreateInfo, &space);
		//	if (XR_SUCCEEDED(res))
		//	{
		//		g_visualizedSpaces.push_back(space);
		//		g_spaceNames[space] = visualizedSpace;
		//	}
		//	else
		//	{
		//		TFE_ERROR("VR", "Failed to create reference space {} with error {}", visualizedSpace, res);
		//	}
		//}
	}

	InputState g_input;

	bool PrepareXrActions()
	{
		// Create an action set.
		{
			XrActionSetCreateInfo actionSetInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
			strcpy_s(actionSetInfo.actionSetName, "moving_around");
			strcpy_s(actionSetInfo.localizedActionSetName, "Moving around");
			actionSetInfo.priority = 0;
			CHECK_XR_RESULT(xrCreateActionSet(mInstance, &actionSetInfo, &g_input.actionSet));
		}

		// Get the XrPath for the left and right hands - we will use them as subaction paths.
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left", &g_input.handSubactionPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right", &g_input.handSubactionPath[Side::Right]));

		// Create actions.
		{
			// Create an input action for grabbing objects with the left and right hands.
			XrActionCreateInfo actionInfo{ XR_TYPE_ACTION_CREATE_INFO };
			actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			strcpy_s(actionInfo.actionName, "grab_object");
			strcpy_s(actionInfo.localizedActionName, "Grab Object");
			actionInfo.countSubactionPaths = uint32_t(g_input.handSubactionPath.size());
			actionInfo.subactionPaths = g_input.handSubactionPath.data();
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.grabAction));

			// Create an input action getting the left and right hand poses.
			actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
			strcpy_s(actionInfo.actionName, "hand_pose");
			strcpy_s(actionInfo.localizedActionName, "Hand Pose");
			actionInfo.countSubactionPaths = uint32_t(g_input.handSubactionPath.size());
			actionInfo.subactionPaths = g_input.handSubactionPath.data();
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.poseAction));

			// Create an input action for eye gaze.
			if (mEyeTrackingSupported)
			{
				actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
				strcpy_s(actionInfo.actionName, "eye_gaze");
				strcpy_s(actionInfo.localizedActionName, "Eye Gaze");
				actionInfo.countSubactionPaths = 0;
				actionInfo.subactionPaths = nullptr;// g_input.handSubactionPath.data();
				CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.eyeGazeAction));
			}

			// Create output actions for vibrating the left and right controller.
			actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
			strcpy_s(actionInfo.actionName, "vibrate_hand");
			strcpy_s(actionInfo.localizedActionName, "Vibrate Hand");
			actionInfo.countSubactionPaths = uint32_t(g_input.handSubactionPath.size());
			actionInfo.subactionPaths = g_input.handSubactionPath.data();
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.vibrateAction));

			// Create input actions for quitting the session using the left and right controller.
			// Since it doesn't matter which hand did this, we do not specify subaction paths for it.
			// We will just suggest bindings for both hands, where possible.
			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "quit_session");
			strcpy_s(actionInfo.localizedActionName, "Quit Session");
			actionInfo.countSubactionPaths = 0;
			actionInfo.subactionPaths = nullptr;
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.quitAction));

			// Menu action.
			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "menu_click_left");
			strcpy_s(actionInfo.localizedActionName, "Menu Click Left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.menuActionLeft));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "menu_click_right");
			strcpy_s(actionInfo.localizedActionName, "Menu Click Right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.menuActionRight));

			// Shoulder action.
			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "shoulder_click_left");
			strcpy_s(actionInfo.localizedActionName, "Shoulder Click Left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.shoulderActionLeft));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "shoulder_click_right");
			strcpy_s(actionInfo.localizedActionName, "Shoulder Click Right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.shoulderActionRight));

			// Trackpad action.
			actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
			strcpy_s(actionInfo.actionName, "trackpad_left");
			strcpy_s(actionInfo.localizedActionName, "Trackpad Left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.trackpadActionLeft));

			actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
			strcpy_s(actionInfo.actionName, "trackpad_right");
			strcpy_s(actionInfo.localizedActionName, "Trackpad Right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.trackpadActionRight));

			// Create an input action for moving camera.
			actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
			strcpy_s(actionInfo.actionName, "move_camera");
			strcpy_s(actionInfo.localizedActionName, "Move Camera");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.moveAction));

			// Create an input action for strafing camera.
			actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
			strcpy_s(actionInfo.actionName, "rotate_camera");
			strcpy_s(actionInfo.localizedActionName, "Rotate Camera");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.rotateAction));

			// Create an input action for gripes.
			actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			strcpy_s(actionInfo.actionName, "grip_left");
			strcpy_s(actionInfo.localizedActionName, "Grip left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.gripActionLeft));

			actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
			strcpy_s(actionInfo.actionName, "grip_right");
			strcpy_s(actionInfo.localizedActionName, "Grip right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.gripActionRight));

			// Create an input action for buttons.
			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "thumb_stick_click_left");
			strcpy_s(actionInfo.localizedActionName, "Thumb stick click left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.thumbStickClickActionLeft));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "thumb_stick_click_right");
			strcpy_s(actionInfo.localizedActionName, "Thumb stick click right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.thumbStickClickActionRight));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "a_button_right");
			strcpy_s(actionInfo.localizedActionName, "Button A right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.aActionRight));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "b_button_right");
			strcpy_s(actionInfo.localizedActionName, "Button B right");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.bActionRight));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "a_button_left");
			strcpy_s(actionInfo.localizedActionName, "Button A left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.aActionLeft));

			actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
			strcpy_s(actionInfo.actionName, "b_button_left");
			strcpy_s(actionInfo.localizedActionName, "Button B left");
			actionInfo.countSubactionPaths = 1;
			actionInfo.subactionPaths = &g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateAction(g_input.actionSet, &actionInfo, &g_input.bActionLeft));
		}

		// https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#_khronos_simple_controller_profile

		std::array<XrPath, 2> selectPath;
		std::array<XrPath, 2> squeezeValuePath;
		std::array<XrPath, 2> squeezeForcePath;
		std::array<XrPath, 2> squeezeClickPath;
		std::array<XrPath, 2> posePath;
		std::array<XrPath, 2> hapticPath;
		std::array<XrPath, 2> menuClickPath;
		std::array<XrPath, 2> shoulderClickPath;
		std::array<XrPath, 2> trackpadValuePath;
		std::array<XrPath, 2> aClickPath;
		std::array<XrPath, 2> bClickPath;
		std::array<XrPath, 2> xClickPath;
		std::array<XrPath, 2> yClickPath;
		std::array<XrPath, 2> triggerValuePath;
		std::array<XrPath, 2> thumbClickPath;
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/select/click", &selectPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/select/click", &selectPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/squeeze/value", &squeezeValuePath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/squeeze/force", &squeezeForcePath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/squeeze/force", &squeezeForcePath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/squeeze/click", &squeezeClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/squeeze/click", &squeezeClickPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/grip/pose", &posePath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/grip/pose", &posePath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/output/haptic", &hapticPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/output/haptic", &hapticPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/menu/click", &menuClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/menu/click", &menuClickPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/shoulder/click", &shoulderClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/shoulder/click", &shoulderClickPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/trackpad", &trackpadValuePath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/trackpad", &trackpadValuePath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/a/click", &aClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/a/click", &aClickPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/b/click", &bClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/b/click", &bClickPath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/x/click", &xClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/y/click", &yClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/trigger/value", &triggerValuePath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/trigger/value", &triggerValuePath[Side::Right]));

		std::array<XrPath, 2> movePath;
		std::array<XrPath, 2> rotatePath;
		//std::array<XrPath, Side::Count> upPath;
		//std::array<XrPath, Side::Count> downPath;
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/thumbstick", &movePath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/thumbstick", &rotatePath[Side::Right]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/thumbstick/click", &thumbClickPath[Side::Left]));
		CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/thumbstick/click", &thumbClickPath[Side::Right]));
		//CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/left/input/a/click", &upPath[Side::Left]));
		//CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/hand/right/input/a/click", &downPath[Side::Right]));

		// Suggest bindings for the Oculus Touch.
		{
			XrPath oculusTouchInteractionProfilePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/interaction_profiles/oculus/touch_controller", &oculusTouchInteractionProfilePath));
			std::vector<XrActionSuggestedBinding> bindings{ {{g_input.grabAction, triggerValuePath[Side::Left]},
															{g_input.grabAction, triggerValuePath[Side::Right]},
															{g_input.poseAction, posePath[Side::Left]},
															{g_input.poseAction, posePath[Side::Right]},
															{g_input.quitAction, menuClickPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Right]},
															{g_input.moveAction, movePath[Side::Left]},
															{g_input.rotateAction, rotatePath[Side::Right]},
															{g_input.gripActionLeft, squeezeValuePath[Side::Left]},
															{g_input.gripActionRight, squeezeValuePath[Side::Right]},
															{g_input.thumbStickClickActionLeft, thumbClickPath[Side::Left]},
															{g_input.thumbStickClickActionRight, thumbClickPath[Side::Right]},
															{g_input.aActionLeft, xClickPath[Side::Left]},
															{g_input.aActionRight, aClickPath[Side::Right]},
															{g_input.bActionLeft, yClickPath[Side::Left]},
															{g_input.bActionRight, bClickPath[Side::Right]}} };
			XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggestedBindings.interactionProfile = oculusTouchInteractionProfilePath;
			suggestedBindings.suggestedBindings = bindings.data();
			suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
			CHECK_XR_RESULT(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings));
		}

		// Suggest bindings for the Valve Index Controller.
		{
			XrPath indexControllerInteractionProfilePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/interaction_profiles/valve/index_controller", &indexControllerInteractionProfilePath));
			std::vector<XrActionSuggestedBinding> bindings{ {{g_input.grabAction, triggerValuePath[Side::Left]},
															{g_input.grabAction, triggerValuePath[Side::Right]},
															{g_input.poseAction, posePath[Side::Left]},
															{g_input.poseAction, posePath[Side::Right]},
															//{g_input.quitAction, menuClickPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Right]},
															{g_input.moveAction, movePath[Side::Left]},
															{g_input.rotateAction, rotatePath[Side::Right]},
															{g_input.gripActionLeft, squeezeValuePath[Side::Left]},
															{g_input.gripActionRight, squeezeValuePath[Side::Right]},
															{g_input.thumbStickClickActionLeft, thumbClickPath[Side::Left]},
															{g_input.thumbStickClickActionRight, thumbClickPath[Side::Right]},
															{g_input.aActionLeft, aClickPath[Side::Left]},
															{g_input.aActionRight, aClickPath[Side::Right]},
															{g_input.bActionLeft, bClickPath[Side::Left]},
															{g_input.bActionRight, bClickPath[Side::Right]}} };
			XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggestedBindings.interactionProfile = indexControllerInteractionProfilePath;
			suggestedBindings.suggestedBindings = bindings.data();
			suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
			CHECK_XR_RESULT(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings));
		}

		// Suggest bindings for the Vive Controller.
		{
			XrPath viveControllerInteractionProfilePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/interaction_profiles/htc/vive_controller", &viveControllerInteractionProfilePath));
			std::vector<XrActionSuggestedBinding> bindings{ {{g_input.grabAction, triggerValuePath[Side::Left]},
															{g_input.grabAction, triggerValuePath[Side::Right]},
															{g_input.poseAction, posePath[Side::Left]},
															{g_input.poseAction, posePath[Side::Right]},
															{g_input.quitAction, menuClickPath[Side::Left]},
															{g_input.quitAction, menuClickPath[Side::Right]},
															{g_input.vibrateAction, hapticPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Right]}} };
			XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggestedBindings.interactionProfile = viveControllerInteractionProfilePath;
			suggestedBindings.suggestedBindings = bindings.data();
			suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
			CHECK_XR_RESULT(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings));
		}

		// Suggest bindings for KHR Simple.
		{
			XrPath khrSimpleInteractionProfilePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/interaction_profiles/khr/simple_controller", &khrSimpleInteractionProfilePath));
			std::vector<XrActionSuggestedBinding> bindings{ {// Fall back to a click input for the grab action.
															{g_input.grabAction, selectPath[Side::Left]},
															{g_input.grabAction, selectPath[Side::Right]},
															{g_input.poseAction, posePath[Side::Left]},
															{g_input.poseAction, posePath[Side::Right]},
															{g_input.quitAction, menuClickPath[Side::Left]},
															{g_input.quitAction, menuClickPath[Side::Right]},
															{g_input.vibrateAction, hapticPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Right]}} };
			XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggestedBindings.interactionProfile = khrSimpleInteractionProfilePath;
			suggestedBindings.suggestedBindings = bindings.data();
			suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
			CHECK_XR_RESULT(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings));
		}

		// Suggest bindings for the Microsoft Mixed Reality Motion Controller.
		{
			XrPath microsoftMixedRealityInteractionProfilePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/interaction_profiles/microsoft/motion_controller", &microsoftMixedRealityInteractionProfilePath));
			std::vector<XrActionSuggestedBinding> bindings{ {//{g_input.grabAction, squeezeClickPath[Side::Left]},
															//{g_input.grabAction, squeezeClickPath[Side::Right]},
															{g_input.poseAction, posePath[Side::Left]},
															{g_input.poseAction, posePath[Side::Right]},
															{g_input.quitAction, menuClickPath[Side::Left]},
															{g_input.quitAction, menuClickPath[Side::Right]},
															{g_input.vibrateAction, hapticPath[Side::Left]},
															{g_input.vibrateAction, hapticPath[Side::Right]}} };
			XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggestedBindings.interactionProfile = microsoftMixedRealityInteractionProfilePath;
			suggestedBindings.suggestedBindings = bindings.data();
			suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
			CHECK_XR_RESULT(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings));
		}

		// Eye tracking interaction.
		if (mEyeTrackingSupported)
		{
			XrPath eyeGazePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/user/eyes_ext/input/gaze_ext/pose", &eyeGazePath));

			XrPath eyeGazeInteractionProfilePath;
			CHECK_XR_RESULT(xrStringToPath(mInstance, "/interaction_profiles/ext/eye_gaze_interaction", &eyeGazeInteractionProfilePath));

			std::vector<XrActionSuggestedBinding> bindings{ {g_input.eyeGazeAction, eyeGazePath} };
			XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggestedBindings.interactionProfile = eyeGazeInteractionProfilePath;
			suggestedBindings.suggestedBindings = bindings.data();
			suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
			CHECK_XR_RESULT(xrSuggestInteractionProfileBindings(mInstance, &suggestedBindings));
		}

		// Create hands spaces.
		{
			XrActionSpaceCreateInfo actionSpaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
			actionSpaceInfo.action = g_input.poseAction;
			actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
			actionSpaceInfo.subactionPath = g_input.handSubactionPath[Side::Left];
			CHECK_XR_RESULT(xrCreateActionSpace(mSession, &actionSpaceInfo, &g_input.handSpace[Side::Left]));
			actionSpaceInfo.subactionPath = g_input.handSubactionPath[Side::Right];
			CHECK_XR_RESULT(xrCreateActionSpace(mSession, &actionSpaceInfo, &g_input.handSpace[Side::Right]));
		}

		// Create eye gaze space.
		if (mEyeTrackingSupported)
		{
			XrActionSpaceCreateInfo actionSpaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
			actionSpaceInfo.action = g_input.eyeGazeAction;
			actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
			CHECK_XR_RESULT(xrCreateActionSpace(mSession, &actionSpaceInfo, &g_input.eyeGazeSpace));
		}

		XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
		attachInfo.countActionSets = 1;
		attachInfo.actionSets = &g_input.actionSet;
		CHECK_XR_RESULT(xrAttachSessionActionSets(mSession, &attachInfo));

		//XrInteractionProfileState interactionProfile{ XR_TYPE_INTERACTION_PROFILE_STATE };
		//XrPath p;
		//xrStringToPath(mInstance, "/interaction_profiles/khr/simple_controller", &p);
		//CHECK_XR_RESULT(xrGetCurrentInteractionProfile(mSession, p, &interactionProfile));

		return true;
	}

//	void PollXrActions()
//	{
//		g_input.handActive = { XR_FALSE, XR_FALSE };
//		g_input.eyeGazeActive = XR_FALSE;
//
//		// Sync actions
//		const XrActiveActionSet activeActionSet{ g_input.actionSet, XR_NULL_PATH };
//		XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
//		syncInfo.countActiveActionSets = 1;
//		syncInfo.activeActionSets = &activeActionSet;
//		CHECK_XR_RESULT(xrSyncActions(mSession, &syncInfo));
//
//		// actions
//		// Get pose and grab action state and start haptic vibrate when hand is 90% squeezed.
//		for (auto hand : { Side::Left, Side::Right })
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//			getInfo.action = g_input.grabAction;
//			getInfo.subactionPath = g_input.handSubactionPath[hand];
//
//			// grab action
//			XrActionStateFloat grabValue{ XR_TYPE_ACTION_STATE_FLOAT };
//			CHECK_XR_RESULT(xrGetActionStateFloat(mSession, &getInfo, &grabValue));
//			if (grabValue.isActive == XR_TRUE)
//			{
//				mControllerState[hand].mIndexTrigger = grabValue.currentState;
//
//				// Scale the rendered hand by 1.0f (open) to 0.5f (fully squeezed).
//				g_input.handScale[hand] = 1.0f - 0.5f * grabValue.currentState;
//				if (grabValue.currentState > 0.9f)
//				{
//					//XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
//					//vibration.amplitude = 0.5;
//					//vibration.duration = XR_MIN_HAPTIC_DURATION;
//					//vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
//
//					//XrHapticActionInfo hapticActionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
//					//hapticActionInfo.action = g_input.vibrateAction;
//					//hapticActionInfo.subactionPath = g_input.handSubactionPath[hand];
//					//CHECK_XR_RESULT(xrApplyHapticFeedback(mSession, &hapticActionInfo, (XrHapticBaseHeader*)&vibration));
//				}
//			}
//			else
//			{
//				mControllerState[hand].mIndexTrigger = 0.0f;
//			}
//
//			// hand pose action
//			getInfo.action = g_input.poseAction;
//			XrActionStatePose poseState{ XR_TYPE_ACTION_STATE_POSE };
//			CHECK_XR_RESULT(xrGetActionStatePose(mSession, &getInfo, &poseState));
//			g_input.handActive[hand] = poseState.isActive;
//			mHandPose[hand].mIsValid = poseState.isActive;
//		}
//
//		// eye gaze pose action
//		if (mEyeTrackingSupported)
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//			getInfo.action = g_input.eyeGazeAction;
//			XrActionStatePose poseState{ XR_TYPE_ACTION_STATE_POSE };
//			CHECK_XR_RESULT(xrGetActionStatePose(mSession, &getInfo, &poseState));
//			g_input.eyeGazeActive = poseState.isActive;
//			mEyeGazePose.mIsValid = poseState.isActive;
//		}
//
//		// move action
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//			getInfo.action = g_input.moveAction;
//			XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
//			CHECK_XR_RESULT(xrGetActionStateVector2f(mSession, &getInfo, &state));
//
//			mControllerState[Side::Left].mThumbStick = { state.currentState.x, state.currentState.y };
//		}
//
//		// rotate action
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//			getInfo.action = g_input.rotateAction;
//			XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
//			CHECK_XR_RESULT(xrGetActionStateVector2f(mSession, &getInfo, &state));
//
//			mControllerState[Side::Right].mThumbStick = { -state.currentState.x, -state.currentState.y };
//		}
//
//		// hand trigger
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//			XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
//
//			getInfo.action = g_input.gripActionLeft;
//			CHECK_XR_RESULT(xrGetActionStateFloat(mSession, &getInfo, &state));
//			mControllerState[Side::Left].mHandTrigger = state.currentState;
//
//			getInfo.action = g_input.gripActionRight;
//			CHECK_XR_RESULT(xrGetActionStateFloat(mSession, &getInfo, &state));
//			mControllerState[Side::Right].mHandTrigger = state.currentState;
//		}
//
//		// trackpad
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//			XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
//
//			getInfo.action = g_input.trackpadActionLeft;
//			CHECK_XR_RESULT(xrGetActionStateVector2f(mSession, &getInfo, &state));
//			mControllerState[Side::Left].mTrackpad = { state.currentState.x, state.currentState.y };
//
//			getInfo.action = g_input.trackpadActionRight;
//			CHECK_XR_RESULT(xrGetActionStateVector2f(mSession, &getInfo, &state));
//			mControllerState[Side::Right].mTrackpad = { state.currentState.x, state.currentState.y };
//		}
//
//		// buttons actions
//		{
//			uint32_t& buttonsLeft = mControllerState[Side::Left].mControllerButtons;
//			uint32_t& buttonsRight = mControllerState[Side::Right].mControllerButtons;
//			buttonsLeft = 0;
//			buttonsRight = 0;
//
//			auto UpdateButtonState = [this](uint32_t& buttons, ControllerButtons flag, const XrAction& action) {
//				XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
//				XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
//
//				getInfo.action = action;
//				CHECK_XR_RESULT(xrGetActionStateBoolean(mSession, &getInfo, &state));
//				if (state.currentState)
//				{
//					buttons |= flag;
//				}
//			};
//
//			UpdateButtonState(buttonsLeft, ControllerButtons::Thumb, g_input.thumbStickClickActionLeft);
//			UpdateButtonState(buttonsRight, ControllerButtons::Thumb, g_input.thumbStickClickActionRight);
//			UpdateButtonState(buttonsLeft, ControllerButtons::A, g_input.aActionLeft);
//			UpdateButtonState(buttonsLeft, ControllerButtons::B, g_input.bActionLeft);
//			UpdateButtonState(buttonsRight, ControllerButtons::A, g_input.aActionRight);
//			UpdateButtonState(buttonsRight, ControllerButtons::B, g_input.bActionRight);
//			UpdateButtonState(buttonsLeft, ControllerButtons::Menu, g_input.menuActionLeft);
//			UpdateButtonState(buttonsRight, ControllerButtons::Menu, g_input.menuActionRight);
//			UpdateButtonState(buttonsLeft, ControllerButtons::Shoulder, g_input.shoulderActionLeft);
//			UpdateButtonState(buttonsRight, ControllerButtons::Shoulder, g_input.shoulderActionRight);
//		}
//
//		// There were no subaction paths specified for the quit action, because we don't care which hand did it.
//		{
//			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, g_input.quitAction, XR_NULL_PATH };
//			XrActionStateBoolean quitValue{ XR_TYPE_ACTION_STATE_BOOLEAN };
//			CHECK_XR_RESULT(xrGetActionStateBoolean(mSession, &getInfo, &quitValue));
//			if ((quitValue.isActive == XR_TRUE) && (quitValue.changedSinceLastSync == XR_TRUE) && (quitValue.currentState == XR_TRUE))
//			{
//				CHECK_XR_RESULT(xrRequestExitSession(mSession));
//			}
//		}
//	}
//
//	void PollXrEvents()
//	{
//		auto PollEvent = [&](XrEventDataBuffer& eventData) -> bool {
//			eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
//			eventData.next = nullptr;
//			return xrPollEvent(mInstance, &eventData) == XR_SUCCESS;
//		};
//
//		XrEventDataBuffer eventData;
//		while (PollEvent(eventData))
//		{
//			switch (eventData.type)
//			{
//			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
//			{
//				mExitRenderLoop = true;
//				mRequestRestart = false;
//				return;
//			}
//			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
//			{
//				const auto stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
//				TFE_ASSERT(mSession != XR_NULL_HANDLE && mSession == stateEvent.session);
//				switch (stateEvent.state)
//				{
//				case XR_SESSION_STATE_READY:
//				{
//					TFE_ASSERT(mSession != XR_NULL_HANDLE);
//					XrSessionBeginInfo sessionBeginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
//					sessionBeginInfo.primaryViewConfigurationType = viewConfigType;
//					CHECK_XR_RESULT(xrBeginSession(mSession, &sessionBeginInfo));
//					mSessionRunning = true;
//					break;
//				}
//				case XR_SESSION_STATE_STOPPING:
//				{
//					mSessionRunning = false;
//					CHECK_XR_RESULT(xrEndSession(mSession));
//					break;
//				}
//				case XR_SESSION_STATE_EXITING:
//				{
//					// Do not attempt to restart, because user closed this session.
//					mExitRenderLoop = true;
//					mRequestRestart = false;
//					break;
//				}
//				case XR_SESSION_STATE_LOSS_PENDING:
//					// Session was lost, so start over and poll for new systemId.
//					mExitRenderLoop = true;
//					mRequestRestart = true;
//				break;
//				default:
//					TFE_DEBUG("VR", "Ignoring session state event {}", GetSessionStateStr(stateEvent.state));
//					break;
//				}
//
//				XrSessionState lastState = XR_SESSION_STATE_UNKNOWN;
//				bool lastSessionRunning = mSessionRunning;
//				if (lastState != stateEvent.state || lastSessionRunning != mSessionRunning)
//				{
//					lastState = stateEvent.state;
//					lastSessionRunning = mSessionRunning;
//					TFE_DEBUG("VR", "stateEvent.state={}, mSessionRunning={}", GetSessionStateStr(stateEvent.state), mSessionRunning);
//				}
//				
//			}
//			break;
//			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
//			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
//			default:
//				TFE_DEBUG("VR", "Ignoring event {}", GetStructureTypeStr(eventData.type));
//				break;
//			}
//		}
//	}
//
//	bool CreateTextureSwapChain(const u32vec2& size)
//	{
//		if (mSwapChainLength > 0)
//		{
//			return true; // already created
//		}
//
//		DestroySwapChain();
//
//		const bool useMultiView = UseMultiView();
//
//		// Allocate a texture swap chain for each eye with the application's EGLContext current.
//		const size_t bufferCount = useMultiView ? 1 : 2;
//		for (size_t eye = 0; eye < bufferCount; eye++)
//		{
//			std::vector<Texture*> textures;
//			XrSwapchain swapchain = OpenXRCreateSwapchain(textures, mSession, mSwapchainFormat,
//				mTargetSize.x, mTargetSize.y, 1, useMultiView ? 2 : 1);
//			mSwapchains[eye] = swapchain;
//			mSwapChainLength = (uint32_t)textures.size();
//
//			for (size_t i = 0; i < textures.size(); i++)
//			{
//				// color
//				mColorTextures[eye].emplace_back(textures[i]);
//
//				// depth
//				Texture::Desc desc;
//				desc.type = useMultiView ? Texture::Type::Texture2DArray : Texture::Type::Texture2D;
//				desc.format = pixelformat::PixelFormat::Depth24Stencil8;
//				desc.usage = ResourceUsage::Dynamic;
//				desc.usageFlags = Texture::UsageDepthStencilAttachmentBIT | Texture::UsageSampledBIT | Texture::UsageTransferDstBIT | Texture::UsageTransferSrcBIT;
//				desc.width = size.x;
//				desc.height = size.y;
//				desc.depth = 1;
//				desc.layerCount = useMultiView ? 2 : 1;
//				desc.mipMapCount = 1;
//				mDepthTextures[eye].emplace_back(ae::renderer::context.mpRenderer->GetGfxAdapter()->CreateTexture(desc, desc.format, nullptr, false, ae::core::Format("VR depth texture eye {}, texture {}", eye, i).c_str()));
//
//				// render target
//				IntrusivePtr<Texture> attachmentTextures[2] = { mDepthTextures[eye][i], mColorTextures[eye][i] };
//				RenderTarget::Desc rtdesc;
//				rtdesc.width = size.x;
//				rtdesc.height = size.y;
//				rtdesc.attachmentMask = RenderTarget::AttachmentFlags::DepthAndStencil | RenderTarget::AttachmentFlags::Color0;
//				mRenderTargets[eye].emplace_back(CreateRenderTarget(rtdesc, reinterpret_cast<Texture* const* const>(attachmentTextures),
//					ae::core::Format("VR render target eye {}, texture {}", eye, i).c_str(), nullptr, useMultiView));
//			}
//		}
//
//		return true;
//	}
//
	uint32_t GetTextureSwapChainLength()
	{
		return mSwapChainLength;
	}

	void DestroySwapChain()
	{
		for (size_t eye = 0; eye < 2; eye++)
		{
			//mRenderTargets[eye].clear();
			//mColorTextures[eye].clear();
			//mDepthTextures[eye].clear();

			if (mSwapchains[eye])
			{
				CHECK_XR_RESULT(xrDestroySwapchain(mSwapchains[eye]));
				mSwapchains[eye] = nullptr;
			}
		}
		mSwapChainLength = 0;
	}

	bool UseMultiView()
	{
		return true;
	}

	bool IsFeatureSupported(Feature feature)
	{
		if (feature == Feature::EyeTracking)
		{
			return mEyeTrackingSupported;
		}
		else if (feature == Feature::Passthrough)
		{
			return mPassthroughSupported;
		}

		return false;
	}

//	UpdateStatus UpdateFrame(const Camera& camera, float userScale)
//	{
//		if (!CreateTextureSwapChain(mTargetSize))
//		{
//			return UpdateStatus::ShouldQuit;
//		}
//
//		mUserScale = userScale;
//
//		PollXrEvents();
//		if (mExitRenderLoop)
//		{
//			return UpdateStatus::ShouldQuit;
//		}
//
//		if (mSessionRunning)
//		{
//			PollXrActions();
//		}
//		else
//		{
//			TFE_DEBUG("VR", "UpdateStatus::NotVisible because !mSessionRunning");
//			return UpdateStatus::NotVisible;
//		}
//
//		XrFrameWaitInfo frameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
//		CHECK_XR_RESULT(xrWaitFrame(mSession, &frameWaitInfo, &mFrameState));
//
//		XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
//		CHECK_XR_RESULT(xrBeginFrame(mSession, &frameBeginInfo));
//
//		if (mFrameState.shouldRender != XR_TRUE)
//		{
//			TFE_DEBUG("VR", "UpdateStatus::ShouldNotRender because mFrameState.shouldRender != XR_TRUE");
//			return UpdateStatus::ShouldNotRender;
//		}
//
//		XrViewState viewState{ XR_TYPE_VIEW_STATE };
//		uint32_t viewCapacityInput = (uint32_t)mViewConfigViews.size();
//		uint32_t viewCountOutput;
//
//		XrViewLocateInfo viewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
//		viewLocateInfo.viewConfigurationType = viewConfigType;
//		viewLocateInfo.displayTime = mFrameState.predictedDisplayTime;
//		viewLocateInfo.space = mAppSpace;
//
//		static std::vector<XrView> views(viewCapacityInput, { XR_TYPE_VIEW });
//
//		CHECK_XR_RESULT(xrLocateViews(mSession, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, views.data()));
//		if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
//			(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0)
//		{
//			TFE_DEBUG("VR", "UpdateStatus::NotVisible because no valid tracking poses");
//			return UpdateStatus::NotVisible; // There is no valid tracking poses for the views.
//		}
//
//		TFE_ASSERT(viewCountOutput == viewCapacityInput);
//		TFE_ASSERT(viewCountOutput == mViewConfigViews.size());
//
//		mProjectionLayerViews.resize(viewCountOutput);
//
//		const mat4 cameraLtw{ camera.GetTransformation().BuildMatrix() };
//		const vec3& cameraPosition = cameraLtw.GetTranslation();
//		const XrVector3f scaleVec{ 1.f, 1.f, 1.f };
//		const quat cameraOrientation{ cameraLtw };
//
//		const bool useMultiview = UseMultiView();
//
//		// headset/eyes
//		{
//			for (size_t viewIndex = 0; viewIndex < Side::Count; viewIndex++)
//			{
//				XrCompositionLayerProjectionView& layerView = mProjectionLayerViews[viewIndex];
//				layerView = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
//				layerView.pose = views[viewIndex].pose;
//				layerView.fov = views[viewIndex].fov;
//				layerView.subImage.swapchain = useMultiview ? mSwapchains[0] : mSwapchains[viewIndex];
//				layerView.subImage.imageRect.offset = { 0, 0 };
//				layerView.subImage.imageRect.extent = { (int32_t)mTargetSize.x, (int32_t)mTargetSize.y };
//				layerView.subImage.imageArrayIndex = useMultiview ? (uint32_t)viewIndex : 0;
//
//				const auto& pose = layerView.pose;
//				XrMatrix4x4f proj;
//				XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_OPENGL, layerView.fov, camera.GetFrustum().GetNearPlane(), camera.GetFrustum().GetFarPlane());
//				mProj[viewIndex] = mat4{ proj.m };
//
//				// by matrix
//				//XrMatrix4x4f toView;
//				//XrMatrix4x4f_CreateTranslationRotationScale(&toView, &pose.position, &pose.orientation, &scaleVec);
//				////XrMatrix4x4f view;
//				////XrMatrix4x4f_InvertRigidBody(&view, &toView);
//				//mEyeLtw[viewIndex] = mat4{ toView.m } * cameraLtw;
//
//				// by quat
//				const quat orientation = quat{ layerView.pose.orientation.w, layerView.pose.orientation.x, layerView.pose.orientation.y, layerView.pose.orientation.z };
//				const vec3 position = vec3{ layerView.pose.position.x, layerView.pose.position.y, layerView.pose.position.z };
//				//const quat f = cameraOrientation * orientation;
//				//const vec3 p = mUserScale * (cameraOrientation * position) + cameraPosition;
//				//mEyePose[viewIndex].mRotation = f;
//				//mEyePose[viewIndex].mPosition = p;
//				//mEyePose[viewIndex].mTransformation = { mat3{mEyePose[viewIndex].mRotation}, mEyePose[viewIndex].mPosition };
//
//				mEyePose[viewIndex].mRotationLocal = orientation;
//				mEyePose[viewIndex].mPositionLocal = position;
//				mEyePose[viewIndex].mTransformationLocal = { mat3{mEyePose[viewIndex].mRotationLocal}, mEyePose[viewIndex].mPositionLocal };
//			}
//
//			// world transformation & user scale
//			{
//				for (size_t viewIndex = 0; viewIndex < Side::Count; viewIndex++)
//				{
//					const quat& orientation = mEyePose[viewIndex].mRotationLocal;
//					const vec3& position = mEyePose[viewIndex].mPositionLocal;
//
//					const quat f = cameraOrientation * orientation;
//					const vec3 p = cameraOrientation * (mUserScale * position) + cameraPosition;
//					mEyePose[viewIndex].mRotation = f;
//					mEyePose[viewIndex].mPosition = p;
//					mEyePose[viewIndex].mTransformation = { mat3{mEyePose[viewIndex].mRotation}, mEyePose[viewIndex].mPosition };
//				}
//			}
//		}
//
//		// For each locatable space that we want to visualize, render a 25cm cube.
//		//std::vector<Space> spaces;
//
//		//for (XrSpace visualizedSpace : g_visualizedSpaces)
//		//{
//		//	XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
//		//	XrResult res = xrLocateSpace(visualizedSpace, g_appSpace, frameState.predictedDisplayTime, &spaceLocation);
//		//	if (XR_UNQUALIFIED_SUCCESS(res))
//		//	{
//		//		if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
//		//			(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
//		//		{
//		//			spaces.push_back(Space{ spaceLocation.pose, {0.25f, 0.25f, 0.25f}, g_spaceNames[visualizedSpace] });
//		//		}
//		//	}
//		//	else
//		//	{
//		//		// Tracking loss is expected when the hand is not active so only log a message
//		//		// if the hand is active.
//		//		//if (g_input.handActive[hand] == XR_TRUE) // TODO:
//		//		{
//		//			TFE_INFO("VR", "Unable to locate a visualized reference space in app space: {}", res);
//		//		}
//		//	}
//		//}
//
//		// controllers
//		{
//			//// Render a 10cm cube scaled by grabAction for each hand. Note renderHand will only be
//			//// true when the application has focus.
//			///// @todo Remove these if you do not want to draw things in hand space.
//			const char* handName[] = { "left", "right" };
//			for (auto hand : { Side::Left, Side::Right })
//			{
//				XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
//				XrResult res = xrLocateSpace(g_input.handSpace[hand], mAppSpace, mFrameState.predictedDisplayTime, &spaceLocation);
//				CHECK_XR_RESULT(res);
//				if (XR_UNQUALIFIED_SUCCESS(res))
//				{
//					if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
//						(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
//					{
//						//float scale = 0.1f * g_input.handScale[hand];
//						//std::string name = handName[hand]; name += "Hand";
//						//spaces.push_back(Space{ spaceLocation.pose, {scale, scale, scale},name });
//
//						// by matrix
//						//XrMatrix4x4f ltw;
//						//constexpr XrVector3f sc{ 1.0f, 1.0f, 1.0f };
//						//XrMatrix4x4f_CreateTranslationRotationScale(&ltw, &spaceLocation.pose.position, &spaceLocation.pose.orientation, &sc);
//						//const mat4 m = mat4{ ltw.m } * cameraLtw;
//						//mHandPose[hand].mTransformation = m;
//
//						// by quat
//						const quat orientation = quat{ spaceLocation.pose.orientation.w, spaceLocation.pose.orientation.x, spaceLocation.pose.orientation.y, spaceLocation.pose.orientation.z };
//						const vec3 position = vec3{ spaceLocation.pose.position.x, spaceLocation.pose.position.y, spaceLocation.pose.position.z };
//						//const quat f = cameraOrientation * orientation;
//						//const vec3 p = (cameraOrientation * position) + cameraPosition;
//						//mHandPose[hand].mRotation = f;
//						//mHandPose[hand].mPosition = p;
//						//mHandPose[hand].mTransformation = { mat3{mHandPose[hand].mRotation}, mHandPose[hand].mPosition };
//
//						mHandPose[hand].mRotationLocal = orientation * quat::FromAngleAndAxis(Radians(-90.0f), vec3{ 1, 0, 0 }); // make forward from -y to -z
//						mHandPose[hand].mPositionLocal = position;
//						mHandPose[hand].mTransformationLocal = { mat3{mHandPose[hand].mRotationLocal}, mHandPose[hand].mPositionLocal };
//					}
//				}
//				else
//				{
//					// Tracking loss is expected when the hand is not active so only log a message if the hand is active.
//					if (g_input.handActive[hand] == XR_TRUE)
//					{
//						TFE_DEBUG("VR", "Unable to locate {} hand action space in app space: {}", handName[hand], res);
//					}
//				}
//			}
//
//			// world transformation & user scale
//			{
//				for (size_t viewIndex = 0; viewIndex < Side::Count; viewIndex++)
//				{
//					const quat& orientation = mHandPose[viewIndex].mRotationLocal;
//					const vec3& position = mHandPose[viewIndex].mPositionLocal;
//
//					const quat f = cameraOrientation * orientation;
//					const vec3 p = cameraOrientation * (mUserScale * position) + cameraPosition;
//					mHandPose[viewIndex].mRotation = f;
//					mHandPose[viewIndex].mPosition = p;
//					mHandPose[viewIndex].mTransformation = { mat3{mHandPose[viewIndex].mRotation}, mHandPose[viewIndex].mPosition };
//				}
//			}
//		}
//
//		// eye gaze
//		if (mEyeTrackingSupported)
//		{
//			mat4 middle = mEyePose[0].mTransformation;
//			//vec3 middlepw = 0.5f * (mEyePose[0].mTransformation.GetTranslation() + mEyePose[1].mTransformation.GetTranslation());
//			vec3 middlepw = 0.5f * (mEyePose[0].mPosition + mEyePose[1].mPosition);
//			middle.SetTranslation(middlepw);
//
//			XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
//			XrResult res = xrLocateSpace(g_input.eyeGazeSpace, mAppSpace, mFrameState.predictedDisplayTime, &spaceLocation);
//			CHECK_XR_RESULT(res);
//			if (XR_UNQUALIFIED_SUCCESS(res))
//			{
//				if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
//					(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
//				{
//					if (spaceLocation.pose.position.x != 0.0f || spaceLocation.pose.position.y != 0.0f || spaceLocation.pose.position.z != 0.0f)
//					{
//						std::ignore = 5;
//					}
//
//					// by matrix
//					XrMatrix4x4f ltw;
//					constexpr XrVector3f sc{ 1.0f, 1.0f, 1.0f };
//					XrMatrix4x4f_CreateTranslationRotationScale(&ltw, &spaceLocation.pose.position, &spaceLocation.pose.orientation, &sc);
//
//					mat4 mirror = mat4::GetIdentity();
//					mirror.SetYAxis({ 0, -1, 0 });
//
//					mat4 m = mat4{ ltw.m } * mirror * middle;
//					//mEyeGazePose.mTransformation = mat4::GetIdentity().RotAroundX(Radians(-90.0f)) * m ;
//					mat4 tr = mat4::GetIdentity().RotAroundX(Radians(-90.0f)) * m;
//
//					// by quat
//					// TODO: add mirror & rot -90 as with matrices
//					const quat orientation = quat{ spaceLocation.pose.orientation.w, spaceLocation.pose.orientation.x, spaceLocation.pose.orientation.y, spaceLocation.pose.orientation.z };
//					const vec3 position = vec3{ spaceLocation.pose.position.x, spaceLocation.pose.position.y, spaceLocation.pose.position.z };
//					const quat f = cameraOrientation * orientation;
//					const vec3 p = cameraOrientation * position + middlepw/*cameraPosition*/;
//					mEyeGazePose.mRotation = f;
//					mEyeGazePose.mPosition = p;
//					mEyeGazePose.mTransformation = { mat3{mEyeGazePose.mRotation}, mEyeGazePose.mPosition };
//
//					mEyeGazePose.mRotationLocal = quat{ orientation.w, orientation.x, orientation.y, orientation.z };
//					mEyeGazePose.mPositionLocal = vec3{ position.x, position.y, position.z };
//					mEyeGazePose.mTransformationLocal = { mat3{mEyeGazePose.mRotationLocal}, mEyeGazePose.mPositionLocal };
//
//					mEyeGazePose.mTransformation = tr;
//				}
//			}
//			else
//			{
//				// Tracking loss is expected when the hand is not active so only log a message if the eye tracking is active.
//				if (g_input.eyeGazeActive == XR_TRUE)
//				{
//					TFE_DEBUG("VR", "Unable to locate eye gaze action space in app space: {}", res);
//				}
//			}
//		}
//
//		return UpdateStatus::Ok;
//	}
//
//	bool UpdateView(uint32_t viewIndex)
//	{
//		if (!mSessionRunning || !mFrameState.shouldRender)
//		{
//			return true;
//		}
//		//const bool useMultiView = UseMultiView();
//		//const size_t bufferCount = useMultiView ? 1 : 2;
//		//for (size_t eye = 0; eye < bufferCount; eye++)
//		{
//			// Each view has a separate swapchain which is acquired, rendered to, and released.
//			XrSwapchain& swapchain = mSwapchains[viewIndex];
//
//			XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
//			CHECK_XR_RESULT(xrAcquireSwapchainImage(swapchain, &acquireInfo, &mTextureSwapChainIndex));
//
//			XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
//			waitInfo.timeout = XR_INFINITE_DURATION;
//			CHECK_XR_RESULT(xrWaitSwapchainImage(swapchain, &waitInfo));
//		}
//
//		return true;
//	}
//
//	void Commit(int eye)
//	{
//		if (!mSessionRunning || !mFrameState.shouldRender)
//		{
//			return;
//		}
//
//		XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
//		CHECK_XR_RESULT(xrReleaseSwapchainImage(mSwapchains[eye], &releaseInfo));
//	}
//
//	bool SubmitFrame()
//	{
//		if (!mSessionRunning)
//		{
//			return true;
//		}
//
//		// Set-up the compositor layers for this frame.
//		// NOTE: Multiple independent layers are allowed, but they need to be added
//		// in a depth consistent order.
//
//		static std::vector<XrCompositionLayerBaseHeader*> layers;
//		layers.clear();
//
//		if (mFrameState.shouldRender == XR_TRUE)
//		{
//			// passthrough layer is backmost layer (if available)
//			if (mPassthroughLayer != XR_NULL_HANDLE)
//			{
//				static XrCompositionLayerPassthroughFB compositionPassthroughLayer{ XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB };
//				compositionPassthroughLayer.flags = 0;//XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
//				compositionPassthroughLayer.layerHandle = mPassthroughLayer;
//				layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&compositionPassthroughLayer));
//			}
//
//			static XrCompositionLayerProjection projectionLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
//			projectionLayer.space = mAppSpace;
//			projectionLayer.viewCount = (uint32_t)mProjectionLayerViews.size();
//			projectionLayer.views = mProjectionLayerViews.data();
//			projectionLayer.layerFlags = layers.empty() ? 0 : XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT | XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
//			layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer));
//		}
//		else
//		{
//			//TFE_DEBUG("VR", "mFrameState.shouldRender==FALSE in OpenXR::SubmitFrame()");
//		}
//
//		XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO };
//		frameEndInfo.displayTime = mFrameState.predictedDisplayTime;
//#if defined(AE_DEVICE_ML2)
//		frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
//#else
//		frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
//#endif
//		frameEndInfo.layerCount = (uint32_t)layers.size();
//		frameEndInfo.layers = layers.data();
//		CHECK_XR_RESULT(xrEndFrame(mSession, &frameEndInfo));
//
//		return true;
//	}
//
//	RenderTarget* GetCurrentTarget(int eye)
//	{
//		if (!CreateTextureSwapChain(mTargetSize))
//		{
//			return nullptr;
//		}
//		return mRenderTargets[eye][mTextureSwapChainIndex].get();
//	}
//
//	const Mat4& GetEyeProj(int eye) const
//	{
//		return mProj[eye];
//	}
//
//	const Mat4& GetEyeLtw(int eye) const
//	{
//		return mEyePose[eye].mTransformation;
//	}
//
//	const Pose& GetEyePose(int eye) const
//	{
//		return mEyePose[eye];
//	}
//
//	const Pose& GetHandPose(int hand) const
//	{
//		//if (hand == 1)
//		//	return mEyeGazePose;
//		 
//		//static Pose pose;
//		//return pose;
//		return mHandPose[hand];
//	}
//
//	void ApplyHapticFeedback(int hand, const HapticVibration& vibration)
//	{
//		hand = std::clamp(hand, 0, 1);
//		XrHapticVibration hapticVibration{ XR_TYPE_HAPTIC_VIBRATION };
//		hapticVibration.amplitude = std::clamp(vibration.mAmplitude, 0.0f, 1.0f);
//		const double durationInNs = (double)vibration.mDuration * 1000000000.0; // seconds to nanoseconds
//		hapticVibration.duration = vibration.mDuration < 0.0f ? XR_MIN_HAPTIC_DURATION : (int64_t)durationInNs;
//		hapticVibration.frequency = vibration.mFrequency <= 0.0f ? XR_FREQUENCY_UNSPECIFIED : vibration.mFrequency;
//
//		XrHapticActionInfo hapticActionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
//		hapticActionInfo.action = g_input.vibrateAction;
//		hapticActionInfo.subactionPath = g_input.handSubactionPath[hand];
//		CHECK_XR_RESULT(xrApplyHapticFeedback(mSession, &hapticActionInfo, (XrHapticBaseHeader*)&hapticVibration));
//	}
//
//	const ControllerState& GetControllerState(int hand) const
//	{
//		return mControllerState[hand];
//	}
//
//	const Pose& GetEyeGazePose() const
//	{
//		return mEyeGazePose;
//	}
//
//	void StartPassthrough()
//	{
//		if (mPassthroughLayer != XR_NULL_HANDLE || !mPassthroughSupported)
//		{
//			return; // already started or not supported
//		}
//
//		CHECK_XR_RESULT2(xrPassthroughStartFB(mPassthrough));
//
//		XrPassthroughLayerCreateInfoFB passthroughLayerConfig{ XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
//		passthroughLayerConfig.passthrough = mPassthrough;
//		passthroughLayerConfig.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB; // TODO: calls xrPassthroughLayerResumeFB?
//		passthroughLayerConfig.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
//		CHECK_XR_RESULT2(xrCreatePassthroughLayerFB(mSession, &passthroughLayerConfig, &mPassthroughLayer));
//		//CHECK_XR_RESULT2(xrPassthroughLayerResumeFB(mPassthroughLayer));
//
//		mOnPassthrough.emit(this, true);
//	}

	void StopPassthrough()
	{
		if (mPassthroughLayer == XR_NULL_HANDLE || !mPassthroughSupported)
		{
			return; // already stopped or not supported
		}

		if (mPassthroughLayer != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrDestroyPassthroughLayerFB(mPassthroughLayer));
			mPassthroughLayer = XR_NULL_HANDLE;
		}

		if (mPassthrough != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrPassthroughPauseFB(mPassthrough));
		}

		//mOnPassthrough.emit(this, false);
	}

	bool IsPassthroughEnabled()
	{
		return mPassthroughLayer != XR_NULL_HANDLE;
	}
} // openxr
}

using namespace openxr;

namespace vr
{
	bool Initialize(vr::Gfx gfx)
	{
		mGfx = gfx;

		if (PrepareXrInstance()
			&& PrepareXrSystem()
			&& PrepareXrSession()
			&& PrepareXrViewConfigViews()
			&& PrepareXrReferenceSpaces()
			&& PrepareXrActions())
		{
			mInitialized = true;
		}

		return mInitialized;
	}

	bool IsInitialized()
	{
		return mInitialized;
	}

	void Deinitialize()
	{
		::openxr::DestroySwapChain();

		StopPassthrough();

		if (mAppSpace != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrDestroySpace(mAppSpace));
		}

		if (mPassthrough != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrDestroyPassthroughFB(mPassthrough));
		}

		if (mSession != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrDestroySession(mSession));
		}

		if (mMessenger != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrDestroyDebugUtilsMessengerEXT(mMessenger));
		}

		if (mInstance != XR_NULL_HANDLE)
		{
			CHECK_XR_RESULT(xrDestroyInstance(mInstance));
		}
	}
} // vr
