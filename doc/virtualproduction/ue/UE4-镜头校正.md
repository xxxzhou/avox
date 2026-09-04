> 整理自 aocec 仓库 `doc/ue4/UE4镜头校正.md`, 2026-09 同步。文中残留的 `../../code/`、`../../glsl/`、`../../assets/`、`../../UE4Test/` 等相对路径指向 aocec 仓库对应文件。

# 镜头校正

## 现有校正改进

内外参都可以做到非常方便,主要是偏移部分.下面分别是这二部分的现在UE4的相关实现.

根据现在实现,有二个方式优化考虑.

[传感器外参标定总结](https://zhuanlan.zhihu.com/p/458993915)

1 按照UE4方式(眼在手外),可以先用镜头收集Redspy固定拍摄点上,然后收集四点以上的UV与三维坐标关系,按照PNP求得镜头在Redspy坐标系下空间.
然后把Redspy移动到需要固定到镜头位置,确实二者的相对位移,搞定,麻烦的是,在固定Redspy与镜头,如何保证镜头不会有移动.

2 先固定摄像机与Redspy之间的相对关系(眼在手上),再固定一个拍摄板(考虑棋盘格).
以棋盘格中心为原点,从摄像机从不同位置与角度拍摄几张图片,并分别记录当时Redspy位置.
这样根据图片UV与对应点上固定位置,用PNP求得到摄像机在棋盘格空间的位置.

1. A Redspy空间下的Redspy位置.
2. B 相机在Redspy空间下的姿态,固定的,我们要解出的姿态.
3. C 相机在标定板下的姿态,求解相机的外参,如上PNP可求.
4. D 标定板在Redspy空间的姿态,这个我们不关心.

Redspy与相机不同位置拍摄几个标定板,构建空间变换回路.

$ D = A{1}*B*C{1}^{-1} = A{2}*B*C{2}^{-1} $ 变换如下 $ (A{2}^{-1}*A{1})*B = B*(C{2}^{-1}*C{1}) $

可以看到,就是一个AX=XB的问题.有使用SVD对矩阵的求解.

姿态分解成旋转与位移,旋转是一个满足正交且行列式为1.AX=XB,分解成旋转与位置二个方程,一般来说,先求出旋转矩阵,然后再用最小二乘法求得位移向量.

## 内外参

插件CameraCalibrationEditor里的UCameraLensDistortionAlgoCheckerboard实现.

插件OpenCVLensCalibration里的Feed实现,在这以UOpenCVLensCalibrator主要代码看下实现过程.

[相机标定函数calibrateCamera使用详解](https://blog.csdn.net/u011574296/article/details/73823569)

``` c++
#if WITH_OPENCV
bool UOpenCVLensCalibrator::Feed(const cv::Mat& InImage)
{
	//Validate image size before going further
	ImageSize = InImage.size();
	if (ImageSize.empty())
	{
		return false;
	}

	std::vector<cv::Point2f> Corners;
	Corners.reserve(BoardSize.height * BoardSize.width);

	// 得到灰度图并查找棋盘格的角点
	cv::Mat Gray;
	cv::cvtColor(InImage, Gray, CV_BGR2GRAY);
	const bool bFound = cv::findChessboardCorners(Gray, BoardSize, Corners, CV_CALIB_CB_ADAPTIVE_THRESH | CV_CALIB_CB_NORMALIZE_IMAGE);
	if (bFound)
	{
		// /亚像素检测,更精确
		cv::cornerSubPix(Gray, Corners, cv::Size(11, 11), cv::Size(-1, -1), cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 0.001));
		ImagePoints.push_back(Corners);

		//Update min/max coordinates to help user cover the whole lens.
		for (const cv::Point2f& Point : Corners)
		{
			MinimumCornerCoordinates.X = FMath::Min(MinimumCornerCoordinates.X, Point.x);
			MinimumCornerCoordinates.Y = FMath::Min(MinimumCornerCoordinates.Y, Point.y);
			MaximumCornerCoordinates.X = FMath::Max(MaximumCornerCoordinates.X, Point.x);
			MaximumCornerCoordinates.Y = FMath::Max(MaximumCornerCoordinates.Y, Point.y);
		}
	}

	return bFound;
}

bool UOpenCVLensCalibrator::CalculateLensParameters(FOpenCVLensDistortionParameters& OutLensDistortionParameters, float& OutMarginOfError, FOpenCVCameraViewInfo& OutCameraViewInfo)
{
#if WITH_OPENCV

	if (ImagePoints.empty())
	{
		return false;
	}

	cv::Mat DistortionCoefficients;
	cv::Mat CameraMatrix = cv::Mat::eye(3, 3, CV_64F);
	OutMarginOfError = FLT_MAX;
	{
		// Reserve space for Rotation and Translation vectors to compute the total error of our calibration. 
		std::vector<cv::Mat> Rvecs, Tvecs;
		Rvecs.reserve(ImagePoints.size());
		Tvecs.reserve(ImagePoints.size());

		// calibrateCamera requires object points for each image capture, even though they're all the same object
		// (the chessboard) in all cases.
		std::vector<std::vector<cv::Point3f>> ObjectPoints;
		ObjectPoints.resize(ImagePoints.size(), BoardPoints);

		if (bUseFisheyeModel)
		{
			//fisheye calibration doesn't like it with 1 image
			if (ImagePoints.size() > 1)
			{
				OutMarginOfError = (float)cv::fisheye::calibrate(ObjectPoints, ImagePoints, ImageSize, CameraMatrix, DistortionCoefficients, Rvecs, Tvecs, cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC + cv::fisheye::CALIB_FIX_SKEW, cv::TermCriteria(CV_TERMCRIT_EPS + CV_TERMCRIT_ITER, 30, 1e-6));
			}
			else
			{
				UE_LOG(LogOpenCVLensCalibration, Warning, TEXT("Fisheye calibration requires at least 2 samples."));
				return false;
			}
		}
		else
		{
			OutMarginOfError = (float)cv::calibrateCamera(ObjectPoints, ImagePoints, ImageSize, CameraMatrix, DistortionCoefficients, Rvecs, Tvecs);
		}
	}
	...
}
#endif
```

## 镜头节点偏移

插件CameraCalibrationEditor里的对应CameraCalibrationCore插件UCameraNodalOffsetAlgo的子类扩展Point/Checkerboard/AlgoAruco三种实现.

计算节点偏移方法用户可以自己添加,实现UCameraNodalOffsetAlgo基本方法GetNodalOffset,各个子类具体实现如何求得节点偏移.

以Points方法为例跟踪相关逻辑.(Checkerboard/AlgoAruco)这二个具体计算也是使用Points里的GetNodalOffset.

在UCameraNodalOffsetAlgoPoints里方法GetNodalOffset具体实现CalculatedOptimalCameraComponentPose收集点击镜头画面上的二维UV与三维校正点的opencv3d坐标.主要利用cv::solvePnP解决.

[cv::solvePnP详解](https://blog.csdn.net/lemonxiaoxiao/article/details/111479116)

``` c++
bool UCameraNodalOffsetAlgoPoints::CalculatedOptimalCameraComponentPose(
	FTransform& OutDesiredCameraTransform, 
	const TArray<TSharedPtr<FCalibrationRowData>>& Rows, 
	FText& OutErrorMessage) const
{
	if (!BasicCalibrationChecksPass(Rows, OutErrorMessage))
	{
		return false;
	}

	const FCameraCalibrationStepsController* StepsController;
	const ULensFile* LensFile;

	if (!ensure(GetStepsControllerAndLensFile(&StepsController, &LensFile)))
	{
		OutErrorMessage = LOCTEXT("ToolNotFound", "Tool not found");
		return false;
	}

	const ULensDistortionModelHandlerBase* DistortionHandler = StepsController->GetDistortionHandler();
	if (!DistortionHandler)
	{
		OutErrorMessage = LOCTEXT("DistortionHandlerNotFound", "No distortion source found");
		return false;
	}

	// Get parameters from the handler
	FLensDistortionState DistortionState = DistortionHandler->GetCurrentDistortionState();

#if WITH_OPENCV

	// Find the pose that minimizes the reprojection error

	// Populate the 3d/2d correlation points

	std::vector<cv::Point3f> Points3d;
	std::vector<cv::Point2f> Points2d;

	TArray<FVector2D> ImagePoints;
    // 收集的镜头点击信息
	for (const TSharedPtr<FCalibrationRowData>& Row : Rows)
	{
		// Convert from UE coordinates to OpenCV coordinates

		FTransform Transform;
		Transform.SetIdentity();
        // 坐标是相应的校正模型上设定点
		Transform.SetLocation(Row->CalibratorPointData.Location);
        // UE4坐标系转到opencv坐标系
		FCameraCalibrationUtils::ConvertUnrealToOpenCV(Transform);

		// Calibrator 3d points
		Points3d.push_back(cv::Point3f(
			Transform.GetLocation().X,
			Transform.GetLocation().Y,
			Transform.GetLocation().Z));
        // 对应是点击镜头对应图片的二维UV信息
		ImagePoints.Add(FVector2D(Row->Point2D.X, Row->Point2D.Y));
	}

	// Populate camera matrix

	cv::Mat CameraMatrix(3, 3, cv::DataType<double>::type);
	cv::setIdentity(CameraMatrix);

	// Note: cv::Mat uses (row,col) indexing.
	//
	//  Fx  0  Cx
	//  0  Fy  Cy
	//  0   0   1
    // 设定镜头坐标系
	CameraMatrix.at<double>(0, 0) = DistortionState.FocalLengthInfo.FxFy.X;
	CameraMatrix.at<double>(1, 1) = DistortionState.FocalLengthInfo.FxFy.Y;

	// The displacement map will correct for image center offset
	CameraMatrix.at<double>(0, 2) = 0.5;
	CameraMatrix.at<double>(1, 2) = 0.5;

	// Manually undistort the 2D image points 得到校正后的二维UV
	TArray<FVector2D> UndistortedPoints;
	UndistortedPoints.AddZeroed(ImagePoints.Num());
	DistortionRenderingUtils::UndistortImagePoints(DistortionHandler->GetDistortionDisplacementMap(), ImagePoints, UndistortedPoints);

	Points2d.reserve(UndistortedPoints.Num());
	for (FVector2D Point : UndistortedPoints)
	{
		Points2d.push_back(cv::Point2f(
			Point.X,
			Point.Y));
	}

	// Solve for camera position
	cv::Mat Rrod = cv::Mat::zeros(3, 1, cv::DataType<double>::type); // Rotation vector in Rodrigues notation. 3x1.
	cv::Mat Tobj = cv::Mat::zeros(3, 1, cv::DataType<double>::type); // Translation vector. 3x1.

	// 使用PNP求解,因为已经校正UV,不需要传入畸变系数
	if (!cv::solvePnP(Points3d, Points2d, CameraMatrix, cv::noArray(), Rrod, Tobj))
	{
		OutErrorMessage = LOCTEXT("SolvePnpFailed", "Failed to resolve a camera position given the data in the calibration rows. Please retry the calibration.");
		return false;
	}

	// Check for invalid data
	{
		const double Tx = Tobj.at<double>(0);
		const double Ty = Tobj.at<double>(1);
		const double Tz = Tobj.at<double>(2);

		const double MaxValue = 1e16;

		if (abs(Tx) > MaxValue || abs(Ty) > MaxValue || abs(Tz) > MaxValue)
		{
			OutErrorMessage = LOCTEXT("DataOutOfBounds", "The triangulated camera position had invalid values, please retry the calibration.");
			return false;
		}
	}

	// Convert to camera pose

	// [R|t]' = [R'|-R'*t]

	// 得到旋转
	cv::Mat Robj;
	cv::Rodrigues(Rrod, Robj); // Robj is 3x3

	// 旋转与位移
	cv::Mat Tcam = -Robj.t() * Tobj;

	// Invert/transpose to get camera orientation
	cv::Mat Rcam = Robj.t();

	// Convert back to UE coordinates

	FMatrix M = FMatrix::Identity;

	// Fill rotation matrix
	for (int32 Column = 0; Column < 3; ++Column)
	{
		M.SetColumn(Column, FVector(
			Rcam.at<double>(Column, 0),
			Rcam.at<double>(Column, 1),
			Rcam.at<double>(Column, 2))
		);
	}

	// Fill translation vector
	M.M[3][0] = Tcam.at<double>(0);
	M.M[3][1] = Tcam.at<double>(1);
	M.M[3][2] = Tcam.at<double>(2);

	OutDesiredCameraTransform.SetFromMatrix(M);
    // opencv的坐标系转成UE4里的坐标系
	FCameraCalibrationUtils::ConvertOpenCVToUnreal(OutDesiredCameraTransform);

	return true;

#else
	{
		OutErrorMessage = LOCTEXT("OpenCVRequired", "OpenCV is required");
		return false;
	}
#endif //WITH_OPENCV
}
```

其UCameraNodalOffsetAlgoCheckerboard::PopulatePoints里点击棋盘格,生成相应的棋盘角点二维与UE4里三维信息,可以反计算出计算机对应在UE4里的姿态信息.
