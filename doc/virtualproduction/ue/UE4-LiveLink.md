> 整理自 aocec 仓库 `doc/ue4/LiveLink.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# UE4 LiveLink

使用LiveLink 管理时间码,UE4能自动对齐数据源,这是一个非常实用的功能,简单分析下这个模块.

LiveLink现简单理解,是针对模型动画/摄像机/灯光等的统一数据结构定义,可以使用UE4本身UDP协议之上的FMessageEndpoint消息管线之类传递数据然后封装,也可以自定义各种消息传递,你只需要把最终数据封装成相应数据结构.

## 类解析

在UE4启用LiveLink插件,对应生成一个LiveLink的客户端,管理数据源与对应个体.

Manage Sources: 对应数据源,表示与别的应用程序连接,为LiveLink提供数据.

Manage Subjects: 数据源中的个体,如一个动画,一个摄像机等,主要由Name/StaticData/FrameData组成.

类说明:

ILiveLinkProvider: 消息发送接收对象,包含一个连接成功的回调.

ULiveLinkBasicRole: 个体的类型,如摄像机,其子类分别指明静态数据类型/动态数据类型,如摄像机ULiveLinkTransformRole,其StaticData/FrameData具体实现对应FLiveLinkCameraStaticData/FLiveLinkCameraFrameData,其FLiveLinkCameraStaticData包含镜头传感器尺寸,是否变焦等不变信息,而FLiveLinkCameraFrameData包含每桢变化数据如位置,FOV,焦段,焦距等信息.

FLiveLinkSubjectFrameData: 包含个体的具体数据,根据UE4里元数据,能把相应空间直接转换成对象各个不同对象,类的设计如下,有兴趣可看.

``` c++
/** Specialization of our wrapped struct for FLiveLinkBaseStaticData */
using FLiveLinkStaticDataStruct = FLiveLinkBaseDataStruct<FLiveLinkBaseStaticData>;

/** Specialization of our wrapped struct for FLiveLinkBaseFrameData */
using FLiveLinkFrameDataStruct = FLiveLinkBaseDataStruct<FLiveLinkBaseFrameData>;

/** Specialization of our wrapped struct for FLiveLinkBaseBlueprintData */
using FLiveLinkBlueprintDataStruct = FLiveLinkBaseDataStruct<FLiveLinkBaseBlueprintData>;

/**
 * Wrapper around FStructOnScope to handle FLiveLinkBaseFrameData
 * Can safely cast to the specific outer type
 */
template<typename BaseType>
class FLiveLinkBaseDataStruct
{
public:
 FLiveLinkBaseDataStruct() = default;

 //Build the wrapper struct using external data location
 FLiveLinkBaseDataStruct(const UScriptStruct* InType, BaseType* InData)
  : WrappedStruct(InType, reinterpret_cast<uint8*>(InData))
 {
 }

 //Build the wrapper struct for a specific type but without any data to initialize it with
 FLiveLinkBaseDataStruct(const UScriptStruct* InType)
  : WrappedStruct(InType)
 {
 }

 FLiveLinkBaseDataStruct(FLiveLinkBaseDataStruct&& InOther)
  : WrappedStruct(MoveTemp(InOther.WrappedStruct))
 {
 }
    // ....
}
/**
 * Wrapper around static and dynamic data to be used when fetching a subject complete data
 */
struct FLiveLinkSubjectFrameData
{
public:
 FLiveLinkStaticDataStruct StaticData;
 FLiveLinkFrameDataStruct FrameData;
};
```

FLiveLinkBaseDataStruct是一个包含空间与对应空间元数据结构UScriptStruct,根据这个结构,可以直接把这空间转对相应类对象,UScriptStruct可以对应父类FLiveLinkBaseFrameData/子类FLiveLinkCameraFrameData,只要UScriptStruct,就能查到对应类型并正确转换.

## 延迟平滑

在文件(Engine\Plugins\Animation\LiveLink\Source\LiveLink\Private\LiveLinkSubject.cpp)中有针对延迟与平滑的处理.

## 使用流程

在一个CineCameraActor下挂个ULiveLinkComponentController组件,选择对应源,自动根据你填充源数据得到角色相关信息,我们选择摄像机类型角色,会自动控制CineCameraActor下的CameraComponent组件.

具体到ULiveLinkComponentController::TickComponent下,找到ULiveLinkControllerBase对应具体角色实现ULiveLinkCameraController,传递给ULiveLinkCameraController的Tick处理具体的FLiveLinkSubjectFrameData数据,这些走向可以让你看到你设置的LiveLink结构是如何具体影响到对应UE4实体.

先壤的UE4插件功能不完善以及设点,其UDP/VRPN都不能带入时间码,其VRPN crash问题一直得到不到解决.现简单分析UE4 LiveLink模块[LiveLink](../ue/UE4-LiveLink.md),根据其LiveLinkFreeD改写我们自己的LiveLinkTvp模块,其设定能传入时间码到UE4的LiveLink以及不crash都已经满足.

``` c++
// LiveLinkTvp 写入数据到LiveLink
uint32 FLiveLinkTvpSource::Run()
{
	const FTimespan SocketTimeout(FTimespan::FromMilliseconds(10));
	bool bFirst = true;
	while (!Stopping)
	{
		if (Socket && Socket->Wait(ESocketWaitConditions::WaitForRead, SocketTimeout))
		{
			uint32 PendingDataSize = 0;
			while (Socket && Socket->HasPendingData(PendingDataSize))
			{
				int32 ReceivedDataSize = 0;
				if (Socket && Socket->Recv(ReceiveBuffer.GetData(), ReceiveBufferSize, ReceivedDataSize))
				{
					if (ReceivedDataSize > 0)
					{
						char ansiiData[ReceiveBufferSize];
						memcpy(ansiiData, ReceiveBuffer.GetData(), ReceivedDataSize);
						ansiiData[ReceivedDataSize] = 0;
						FString data = ANSI_TO_TCHAR(ansiiData);
						memset(ansiiData, 0, ReceiveBufferSize);

						std::string str = std::string(TCHAR_TO_UTF8(*data));
						std::istringstream s(str);

						double px, py, pz, rx, ry, rz, rw, fov, focusDistance;
						s >> px >> py >> pz >> rx >> ry >> rz >> rw >> fov >> focusDistance;

						int32 hours, minutes, seconds, frames;
						double rate;
						s >> hours >> minutes >> seconds >> frames >> rate;

						// 
						double focusLength, sensorWidth, sensorHeight;
						s >> focusLength >> sensorWidth >> sensorHeight;

						FVector pos(px, py, pz);
						FQuat rot(rx, ry, rz, rw);
						FFrameRate frameRate(rate, 1);
						FTimecode timecode(hours, minutes, seconds, frames,
							FTimecode::UseDropFormatTimecode(frameRate));

						FLiveLinkFrameDataStruct FrameData(FLiveLinkCameraFrameData::StaticStruct());
						FLiveLinkCameraFrameData* CameraFrameData = FrameData.Cast<FLiveLinkCameraFrameData>();
						CameraFrameData->Transform = FTransform(rot, pos);
						// CameraFrameData->FocalLength = FocalLength;
						if(!UserFocusDistance)
						{
							CameraFrameData->FocusDistance = focusDistance;
						}
						if(!UserFocusLenght)
						{
							CameraFrameData->FocalLength = focusLength;
						}
						CameraFrameData->FieldOfView = fov;						
						CameraFrameData->MetaData.SceneTime = FQualifiedFrameTime(timecode, frameRate);
						if (Client)
						{
							if (bFirst)
							{
								bFirst = false;
								FLiveLinkStaticDataStruct StaticData(FLiveLinkCameraStaticData::StaticStruct());
								FLiveLinkCameraStaticData& CameraData = *StaticData.Cast<FLiveLinkCameraStaticData>();
								CameraData.bIsFieldOfViewSupported = true;
								CameraData.bIsFocusDistanceSupported = !UserFocusDistance;
								CameraData.bIsFocalLengthSupported = !UserFocusLenght && focusLength > 0.0;
								CameraData.FilmBackWidth = sensorWidth;
								CameraData.FilmBackHeight = sensorHeight;
								Client->PushSubjectStaticData_AnyThread({ SourceGuid, CameraSubjectName },
									ULiveLinkCameraRole::StaticClass(),
									MoveTemp(StaticData));
							}
							Client->PushSubjectFrameData_AnyThread({ SourceGuid, CameraSubjectName },
								MoveTemp(FrameData));
						}
						FrameCounter++;
					}
				}
			}
		}
	}
	return 0;
}

// UE4 使用LinkLink数据
void ULiveLinkCameraController::Tick(float DeltaTime, const FLiveLinkSubjectFrameData& SubjectData)
{
	// ...
	const FLiveLinkCameraStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkCameraStaticData>();
	const FLiveLinkCameraFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkCameraFrameData>();

	if (StaticData && FrameData)
	{
		if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(AttachedComponent))
		{
			if (StaticData->bIsFieldOfViewSupported && UpdateFlags.bApplyFieldOfView) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); }
			if (StaticData->bIsAspectRatioSupported && UpdateFlags.bApplyAspectRatio) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
			ApplyFIZ(SelectedLensFile, CineCameraComponent, StaticData, FrameData);
			ApplyDistortion(SelectedLensFile, CineCameraComponent, StaticData, FrameData);
		}
	}
	// ...
}
```
