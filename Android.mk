ifneq ($(filter $(LOCAL_PATH),$(PRODUCT_SOONG_NAMESPACES)),)

include $(call all-subdir-makefiles)

endif
