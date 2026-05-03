# ProGuard 规则 - AndroidReShade

# 保留 JNI 接口（native 方法不能混淆）
-keepclasseswithmembernames class * {
    native <methods>;
}

# 保留 ReShadeBridge（JNI 桥接）
-keep class com.reshade.android.jni.ReShadeBridge {
    public static *;
}

# 保留 Android Service（OverlayService 等）
-keep class com.reshade.android.*Service { *; }
-keep class com.reshade.android.*Activity { *; }

# 保留 AIDL / IPC 接口
-keep interface com.reshade.android.** { *; }

# Material Design
-dontwarn com.google.android.material.**
-keep class com.google.android.material.** { *; }
