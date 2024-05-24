#pragma once

namespace vr::openxr::detail
{
	const char* GetSessionStateStr(XrSessionState state);
	const char* GetStructureTypeStr(XrStructureType type);
	const char* GetResultStr(XrResult result);
}
