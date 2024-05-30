#ifdef OPT_VR_MULTIVIEW
#extension GL_OVR_multiview : enable
#ifdef VERTEX_SHADER
layout(num_views = 2) in;
#endif
#endif
