set(COMPONENT_SRCS "cloud_manager.c"
			"app_main.c")
			
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()

component_compile_definitions(SET_TRUSTED_CERT_IN_SAMPLES)
