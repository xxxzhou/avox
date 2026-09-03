# Windows

## SWIG

如果有swig,CMake会根据平台，自动调用SWIG生成封装语言，比如在windows平台，自动生成C#，在android平台自动生成java.

如果没有swig,默认会在windows/CSharp/BuildType生成相应的avox_sharp.dll/AvoxSharp.dll，Android/avox/libs/avox_swig.jar,但是有个问题，可能不是最新版本，有可能与接口不匹配。