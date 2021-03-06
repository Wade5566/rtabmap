/*
Copyright (c) 2010-2014, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rtabmap/gui/CloudViewer.h"

#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/utilite/UMath.h>
#include <rtabmap/utilite/UConversion.h>
#include <rtabmap/core/util3d.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/transforms.h>
#include <QMenu>
#include <QAction>
#include <QtGui/QContextMenuEvent>
#include <QInputDialog>
#include <QtGui/QWheelEvent>
#include <QtGui/QKeyEvent>
#include <QColorDialog>
#include <QtGui/QVector3D>
#include <set>

#include <vtkCamera.h>
#include <vtkRenderWindow.h>

namespace rtabmap {

class MyInteractorStyle: public pcl::visualization::PCLVisualizerInteractorStyle
{
public:
	virtual void Rotate()
	{
		if (this->CurrentRenderer == NULL)
		{
			return;
		}

		vtkRenderWindowInteractor *rwi = this->Interactor;

		int dx = rwi->GetEventPosition()[0] - rwi->GetLastEventPosition()[0];
		int dy = rwi->GetEventPosition()[1] - rwi->GetLastEventPosition()[1];

		int *size = this->CurrentRenderer->GetRenderWindow()->GetSize();

		double delta_elevation = -20.0 / size[1];
		double delta_azimuth = -20.0 / size[0];

		double rxf = dx * delta_azimuth * this->MotionFactor;
		double ryf = dy * delta_elevation * this->MotionFactor;

		vtkCamera *camera = this->CurrentRenderer->GetActiveCamera();
		camera->Azimuth(rxf);
		camera->Elevation(ryf);
		camera->OrthogonalizeViewUp();

		if (this->AutoAdjustCameraClippingRange)
		{
			this->CurrentRenderer->ResetCameraClippingRange();
		}

		if (rwi->GetLightFollowCamera())
		{
			this->CurrentRenderer->UpdateLightsGeometryToFollowCamera();
		}

		//rwi->Render();
	}
};


CloudViewer::CloudViewer(QWidget *parent) :
		QVTKWidget(parent),
		_aLockCamera(0),
		_aFollowCamera(0),
		_aResetCamera(0),
		_aLockViewZ(0),
		_aShowTrajectory(0),
		_aSetTrajectorySize(0),
		_aClearTrajectory(0),
		_aShowFrustum(0),
		_aSetFrustumScale(0),
		_aSetFrustumColor(0),
		_aShowGrid(0),
		_aSetGridCellCount(0),
		_aSetGridCellSize(0),
		_aSetBackgroundColor(0),
		_menu(0),
		_trajectory(new pcl::PointCloud<pcl::PointXYZ>),
		_maxTrajectorySize(100),
		_frustumScale(0.5f),
		_frustumColor(Qt::gray),
		_gridCellCount(50),
		_gridCellSize(1),
		_lastCameraOrientation(0,0,0),
		_lastCameraPose(0,0,0),
		_workingDirectory("."),
		_defaultBgColor(Qt::black),
		_currentBgColor(Qt::black),
		_backfaceCulling(false),
		_frontfaceCulling(false)
{
	this->setMinimumSize(200, 200);

	int argc = 0;
	_visualizer = new pcl::visualization::PCLVisualizer(argc, 0, "PCLVisualizer", vtkSmartPointer<MyInteractorStyle>(new MyInteractorStyle()), false);

	this->SetRenderWindow(_visualizer->getRenderWindow());

	// Replaced by the second line, to avoid a crash in Mac OS X on close, as well as
	// the "Invalid drawable" warning when the view is not visible.
	//_visualizer->setupInteractor(this->GetInteractor(), this->GetRenderWindow());
	this->GetInteractor()->SetInteractorStyle (_visualizer->getInteractorStyle());
	_visualizer->getInteractorStyle()->GetInteractor()->SetDesiredUpdateRate(5.0);

	_visualizer->setCameraPosition(
				-1, 0, 0,
				0, 0, 0,
				0, 0, 1);
#ifndef _WIN32
	// Crash on startup on Windows (vtk issue)
	this->addOrUpdateCoordinate("reference", Transform::getIdentity(), 0.2);
#endif

	//setup menu/actions
	createMenu();

	setMouseTracking(false);
}

CloudViewer::~CloudViewer()
{
	UDEBUG("");
	this->clear();
	delete _visualizer;
}

void CloudViewer::clear()
{
	this->removeAllClouds();
	this->removeAllGraphs();
	this->removeAllCoordinates();
	this->removeAllFrustums();
	this->removeAllTexts();
	this->clearTrajectory();

	this->addOrUpdateCoordinate("reference", Transform::getIdentity(), 0.2);
	if(_aShowFrustum->isChecked())
	{
		this->addOrUpdateFrustum("reference_frustum", Transform::getIdentity(), _frustumScale, _frustumColor);
	}
}

void CloudViewer::createMenu()
{
	_aLockCamera = new QAction("Lock target", this);
	_aLockCamera->setCheckable(true);
	_aLockCamera->setChecked(false);
	_aFollowCamera = new QAction("Follow", this);
	_aFollowCamera->setCheckable(true);
	_aFollowCamera->setChecked(true);
	QAction * freeCamera = new QAction("Free", this);
	freeCamera->setCheckable(true);
	freeCamera->setChecked(false);
	_aLockViewZ = new QAction("Lock view Z", this);
	_aLockViewZ->setCheckable(true);
	_aLockViewZ->setChecked(true);
	_aResetCamera = new QAction("Reset position", this);
	_aShowTrajectory= new QAction("Show trajectory", this);
	_aShowTrajectory->setCheckable(true);
	_aShowTrajectory->setChecked(true);
	_aSetTrajectorySize = new QAction("Set trajectory size...", this);
	_aClearTrajectory = new QAction("Clear trajectory", this);
	_aShowFrustum= new QAction("Show frustum", this);
	_aShowFrustum->setCheckable(true);
	_aShowFrustum->setChecked(false);
	_aSetFrustumScale = new QAction("Set frustum scale...", this);
	_aSetFrustumColor = new QAction("Set frustum color...", this);
	_aShowGrid = new QAction("Show grid", this);
	_aShowGrid->setCheckable(true);
	_aSetGridCellCount = new QAction("Set cell count...", this);
	_aSetGridCellSize = new QAction("Set cell size...", this);
	_aSetBackgroundColor = new QAction("Set background color...", this);

	QMenu * cameraMenu = new QMenu("Camera", this);
	cameraMenu->addAction(_aLockCamera);
	cameraMenu->addAction(_aFollowCamera);
	cameraMenu->addAction(freeCamera);
	cameraMenu->addSeparator();
	cameraMenu->addAction(_aLockViewZ);
	cameraMenu->addAction(_aResetCamera);
	QActionGroup * group = new QActionGroup(this);
	group->addAction(_aLockCamera);
	group->addAction(_aFollowCamera);
	group->addAction(freeCamera);

	QMenu * trajectoryMenu = new QMenu("Trajectory", this);
	trajectoryMenu->addAction(_aShowTrajectory);
	trajectoryMenu->addAction(_aSetTrajectorySize);
	trajectoryMenu->addAction(_aClearTrajectory);

	QMenu * frustumMenu = new QMenu("Frustum", this);
	frustumMenu->addAction(_aShowFrustum);
	frustumMenu->addAction(_aSetFrustumScale);
	frustumMenu->addAction(_aSetFrustumColor);

	QMenu * gridMenu = new QMenu("Grid", this);
	gridMenu->addAction(_aShowGrid);
	gridMenu->addAction(_aSetGridCellCount);
	gridMenu->addAction(_aSetGridCellSize);

	//menus
	_menu = new QMenu(this);
	_menu->addMenu(cameraMenu);
	_menu->addMenu(trajectoryMenu);
	_menu->addMenu(frustumMenu);
	_menu->addMenu(gridMenu);
	_menu->addAction(_aSetBackgroundColor);
}

void CloudViewer::saveSettings(QSettings & settings, const QString & group) const
{
	if(!group.isEmpty())
	{
		settings.beginGroup(group);
	}

	float poseX, poseY, poseZ, focalX, focalY, focalZ, upX, upY, upZ;
	this->getCameraPosition(poseX, poseY, poseZ, focalX, focalY, focalZ, upX, upY, upZ);
	QVector3D pose(poseX, poseY, poseZ);
	QVector3D focal(focalX, focalY, focalZ);
	if(!this->isCameraFree())
	{
		// make camera position relative to target
		Transform T = this->getTargetPose();
		if(this->isCameraTargetLocked())
		{
			T = Transform(T.x(), T.y(), T.z(), 0,0,0);
		}
		Transform F(focalX, focalY, focalZ, 0,0,0);
		Transform P(poseX, poseY, poseZ, 0,0,0);
		Transform newFocal = T.inverse() * F;
		Transform newPose = newFocal * F.inverse() * P;
		pose = QVector3D(newPose.x(), newPose.y(), newPose.z());
		focal = QVector3D(newFocal.x(), newFocal.y(), newFocal.z());
	}
	settings.setValue("camera_pose", pose);
	settings.setValue("camera_focal", focal);
	settings.setValue("camera_up", QVector3D(upX, upY, upZ));

	settings.setValue("grid", this->isGridShown());
	settings.setValue("grid_cell_count", this->getGridCellCount());
	settings.setValue("grid_cell_size", (double)this->getGridCellSize());

	settings.setValue("trajectory_shown", this->isTrajectoryShown());
	settings.setValue("trajectory_size", this->getTrajectorySize());

	settings.setValue("frustum_shown", this->isFrustumShown());
	settings.setValue("frustum_scale", this->getFrustumScale());
	settings.setValue("frustum_color", this->getFrustumColor());

	settings.setValue("camera_target_locked", this->isCameraTargetLocked());
	settings.setValue("camera_target_follow", this->isCameraTargetFollow());
	settings.setValue("camera_free", this->isCameraFree());
	settings.setValue("camera_lockZ", this->isCameraLockZ());

	settings.setValue("bg_color", this->getDefaultBackgroundColor());
	if(!group.isEmpty())
	{
		settings.endGroup();
	}
}

void CloudViewer::loadSettings(QSettings & settings, const QString & group)
{
	if(!group.isEmpty())
	{
		settings.beginGroup(group);
	}

	float poseX, poseY, poseZ, focalX, focalY, focalZ, upX, upY, upZ;
	this->getCameraPosition(poseX, poseY, poseZ, focalX, focalY, focalZ, upX, upY, upZ);
	QVector3D pose(poseX, poseY, poseZ), focal(focalX, focalY, focalZ), up(upX, upY, upZ);
	pose = settings.value("camera_pose", pose).value<QVector3D>();
	focal = settings.value("camera_focal", focal).value<QVector3D>();
	up = settings.value("camera_up", up).value<QVector3D>();
	this->setCameraPosition(pose.x(),pose.y(),pose.z(), focal.x(),focal.y(),focal.z(), up.x(),up.y(),up.z());

	this->setGridShown(settings.value("grid", this->isGridShown()).toBool());
	this->setGridCellCount(settings.value("grid_cell_count", this->getGridCellCount()).toUInt());
	this->setGridCellSize(settings.value("grid_cell_size", this->getGridCellSize()).toFloat());

	this->setTrajectoryShown(settings.value("trajectory_shown", this->isTrajectoryShown()).toBool());
	this->setTrajectorySize(settings.value("trajectory_size", this->getTrajectorySize()).toUInt());

	this->setFrustumShown(settings.value("frustum_shown", this->isFrustumShown()).toBool());
	this->setFrustumScale(settings.value("frustum_scale", this->getFrustumScale()).toDouble());
	this->setFrustumColor(settings.value("frustum_color", this->getFrustumColor()).value<QColor>());

	this->setCameraTargetLocked(settings.value("camera_target_locked", this->isCameraTargetLocked()).toBool());
	this->setCameraTargetFollow(settings.value("camera_target_follow", this->isCameraTargetFollow()).toBool());
	if(settings.value("camera_free", this->isCameraFree()).toBool())
	{
		this->setCameraFree();
	}
	this->setCameraLockZ(settings.value("camera_lockZ", this->isCameraLockZ()).toBool());

	this->setDefaultBackgroundColor(settings.value("bg_color", this->getDefaultBackgroundColor()).value<QColor>());
	if(!group.isEmpty())
	{
		settings.endGroup();
	}

	this->update();
}

bool CloudViewer::updateCloudPose(
		const std::string & id,
		const Transform & pose)
{
	if(_addedClouds.contains(id))
	{
		UDEBUG("Updating pose %s to %s", id.c_str(), pose.prettyPrint().c_str());
		if(_addedClouds.find(id).value() == pose ||
		   _visualizer->updatePointCloudPose(id, pose.toEigen3f()))
		{
			_addedClouds.find(id).value() = pose;
			return true;
		}
	}
	return false;
}

bool CloudViewer::addCloud(
		const std::string & id,
		const pcl::PCLPointCloud2Ptr & binaryCloud,
		const Transform & pose,
		bool rgb,
		bool haveNormals,
		const QColor & color)
{
	int previousColorIndex = -1;
	if(_addedClouds.contains(id))
	{
		previousColorIndex = _visualizer->getColorHandlerIndex(id);
		this->removeCloud(id);
	}

	Eigen::Vector4f origin(pose.x(), pose.y(), pose.z(), 0.0f);
	Eigen::Quaternionf orientation = Eigen::Quaternionf(pose.toEigen3f().rotation());

	// add random color channel
	 pcl::visualization::PointCloudColorHandler<pcl::PCLPointCloud2>::Ptr colorHandler;
	 colorHandler.reset (new pcl::visualization::PointCloudColorHandlerRandom<pcl::PCLPointCloud2> (binaryCloud));
	 if(_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id))
	 {
		QColor c = Qt::gray;
		if(color.isValid())
		{
			c = color;
		}
		colorHandler.reset (new pcl::visualization::PointCloudColorHandlerCustom<pcl::PCLPointCloud2> (binaryCloud, c.red(), c.green(), c.blue()));
		_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);

		// x,y,z
		colorHandler.reset (new pcl::visualization::PointCloudColorHandlerGenericField<pcl::PCLPointCloud2> (binaryCloud, "x"));
		_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);
		colorHandler.reset (new pcl::visualization::PointCloudColorHandlerGenericField<pcl::PCLPointCloud2> (binaryCloud, "y"));
		_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);
		colorHandler.reset (new pcl::visualization::PointCloudColorHandlerGenericField<pcl::PCLPointCloud2> (binaryCloud, "z"));
		_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);

		if(rgb)
		{
			//rgb
			colorHandler.reset(new pcl::visualization::PointCloudColorHandlerRGBField<pcl::PCLPointCloud2>(binaryCloud));
			_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);
		}
		else if(previousColorIndex == 5)
		{
			previousColorIndex = -1;
		}

		if(haveNormals)
		{
			//normals
			colorHandler.reset (new pcl::visualization::PointCloudColorHandlerGenericField<pcl::PCLPointCloud2> (binaryCloud, "normal_x"));
			_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);
			colorHandler.reset (new pcl::visualization::PointCloudColorHandlerGenericField<pcl::PCLPointCloud2> (binaryCloud, "normal_y"));
			_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);
			colorHandler.reset (new pcl::visualization::PointCloudColorHandlerGenericField<pcl::PCLPointCloud2> (binaryCloud, "normal_z"));
			_visualizer->addPointCloud (binaryCloud, colorHandler, origin, orientation, id);
		}
		else if(previousColorIndex > 5)
		{
			previousColorIndex = -1;
		}

		if(previousColorIndex>=0)
		{
			_visualizer->updateColorHandlerIndex(id, previousColorIndex);
		}
		else if(rgb)
		{
			_visualizer->updateColorHandlerIndex(id, 5);
		}
		else if(color.isValid())
		{
			_visualizer->updateColorHandlerIndex(id, 1);
		}

		_addedClouds.insert(id, pose);
		return true;
	}
	return false;
}

bool CloudViewer::addCloud(
		const std::string & id,
		const pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr & cloud,
		const Transform & pose,
		const QColor & color)
{
	pcl::PCLPointCloud2Ptr binaryCloud(new pcl::PCLPointCloud2);
	pcl::toPCLPointCloud2(*cloud, *binaryCloud);
	return addCloud(id, binaryCloud, pose, true, true, color);
}

bool CloudViewer::addCloud(
		const std::string & id,
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
		const Transform & pose,
		const QColor & color)
{
	pcl::PCLPointCloud2Ptr binaryCloud(new pcl::PCLPointCloud2);
	pcl::toPCLPointCloud2(*cloud, *binaryCloud);
	return addCloud(id, binaryCloud, pose, true, false, color);
}

bool CloudViewer::addCloud(
		const std::string & id,
		const pcl::PointCloud<pcl::PointNormal>::Ptr & cloud,
		const Transform & pose,
		const QColor & color)
{
	pcl::PCLPointCloud2Ptr binaryCloud(new pcl::PCLPointCloud2);
	pcl::toPCLPointCloud2(*cloud, *binaryCloud);
	return addCloud(id, binaryCloud, pose, false, true, color);
}

bool CloudViewer::addCloud(
		const std::string & id,
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
		const Transform & pose,
		const QColor & color)
{
	pcl::PCLPointCloud2Ptr binaryCloud(new pcl::PCLPointCloud2);
	pcl::toPCLPointCloud2(*cloud, *binaryCloud);
	return addCloud(id, binaryCloud, pose, false, false, color);
}

bool CloudViewer::addCloudMesh(
	const std::string & id,
	const pcl::PointCloud<pcl::PointXYZ>::Ptr & cloud,
	const std::vector<pcl::Vertices> & polygons,
	const Transform & pose)
{
	if(_addedClouds.contains(id))
	{
		this->removeCloud(id);
	}

	UDEBUG("Adding %s with %d points and %d polygons", id.c_str(), (int)cloud->size(), (int)polygons.size());
	if(_visualizer->addPolygonMesh<pcl::PointXYZ>(cloud, polygons, id))
	{
		_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->LightingOff();
		if(_backfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->BackfaceCullingOn();
		}
		if(_frontfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->FrontfaceCullingOn();
		}
		_visualizer->updatePointCloudPose(id, pose.toEigen3f());
		_addedClouds.insert(id, pose);
		return true;
	}
	return false;
}

bool CloudViewer::addCloudMesh(
	const std::string & id,
	const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
	const std::vector<pcl::Vertices> & polygons,
	const Transform & pose)
{
	if(_addedClouds.contains(id))
	{
		this->removeCloud(id);
	}

	UDEBUG("Adding %s with %d points and %d polygons", id.c_str(), (int)cloud->size(), (int)polygons.size());
	if(_visualizer->addPolygonMesh<pcl::PointXYZRGB>(cloud, polygons, id))
	{
		_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->LightingOff();
		if(_backfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->BackfaceCullingOn();
		}
		if(_frontfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->FrontfaceCullingOn();
		}
		_visualizer->updatePointCloudPose(id, pose.toEigen3f());
		_addedClouds.insert(id, pose);
		return true;
	}
	return false;
}

bool CloudViewer::addCloudMesh(
	const std::string & id,
	const pcl::PointCloud<pcl::PointXYZRGBNormal>::Ptr & cloud,
	const std::vector<pcl::Vertices> & polygons,
	const Transform & pose)
{
	if(_addedClouds.contains(id))
	{
		this->removeCloud(id);
	}

	UDEBUG("Adding %s with %d points and %d polygons", id.c_str(), (int)cloud->size(), (int)polygons.size());
	if(_visualizer->addPolygonMesh<pcl::PointXYZRGBNormal>(cloud, polygons, id))
	{
		_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->LightingOff();
		if(_backfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->BackfaceCullingOn();
		}
		if(_frontfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->FrontfaceCullingOn();
		}
		_visualizer->updatePointCloudPose(id, pose.toEigen3f());
		_addedClouds.insert(id, pose);
		return true;
	}
	return false;
}

bool CloudViewer::addCloudMesh(
	const std::string & id,
	const pcl::PolygonMesh::Ptr & mesh,
	const Transform & pose)
{
	if(_addedClouds.contains(id))
	{
		this->removeCloud(id);
	}

	UDEBUG("Adding %s with %d polygons", id.c_str(), (int)mesh->polygons.size());
	if(_visualizer->addPolygonMesh(*mesh, id))
	{
		_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->LightingOff();
		if(_backfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->BackfaceCullingOn();
		}
		if(_frontfaceCulling)
		{
			_visualizer->getCloudActorMap()->find(id)->second.actor->GetProperty()->FrontfaceCullingOn();
		}
		_visualizer->updatePointCloudPose(id, pose.toEigen3f());
		_addedClouds.insert(id, pose);
		return true;
	}

	return false;
}

bool CloudViewer::addCloudTextureMesh(
	const std::string & id,
	const pcl::TextureMesh::Ptr & textureMesh,
	const Transform & pose)
{
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
	if(_addedClouds.contains(id))
	{
		this->removeCloud(id);
	}

	UDEBUG("Adding %s", id.c_str());
	if(_visualizer->addTextureMesh(*textureMesh, id))
	{
		_visualizer->updatePointCloudPose(id, pose.toEigen3f());
		_addedClouds.insert(id, pose);
		return true;
	}

#endif
	// not implemented on lower version of PCL
	return false;
}

bool CloudViewer::addOccupancyGridMap(
		const cv::Mat & map8U,
		float resolution, // cell size
		float xMin,
		float yMin,
		float opacity)
{
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
	UASSERT(map8U.channels() == 1 && map8U.type() == CV_8U);

	float xSize = float(map8U.cols) * resolution;
	float ySize = float(map8U.rows) * resolution;

	UDEBUG("resolution=%f, xSize=%f, ySize=%f, xMin=%f, yMin=%f", resolution, xSize, ySize, xMin, yMin);
	if(_visualizer->getShapeActorMap()->find("map") == _visualizer->getShapeActorMap()->end())
	{
		_visualizer->removeShape("map");
	}

	if(xSize > 0.0f && ySize > 0.0f)
	{
		pcl::TextureMeshPtr mesh(new pcl::TextureMesh());
		pcl::PointCloud<pcl::PointXYZ> cloud;
		cloud.push_back(pcl::PointXYZ(xMin,       yMin,       0));
		cloud.push_back(pcl::PointXYZ(xSize+xMin, yMin,       0));
		cloud.push_back(pcl::PointXYZ(xSize+xMin, ySize+yMin, 0));
		cloud.push_back(pcl::PointXYZ(xMin,       ySize+yMin, 0));
		pcl::toPCLPointCloud2(cloud, mesh->cloud);

		std::vector<pcl::Vertices> polygons(1);
		polygons[0].vertices.push_back(0);
		polygons[0].vertices.push_back(1);
		polygons[0].vertices.push_back(2);
		polygons[0].vertices.push_back(3);
		polygons[0].vertices.push_back(0);
		mesh->tex_polygons.push_back(polygons);

		// default texture materials parameters
		pcl::TexMaterial material;
		// hack, can we read from memory?
		std::string tmpPath = (_workingDirectory+"/.tmp_map.png").toStdString();
		cv::imwrite(tmpPath, map8U);
		material.tex_file = tmpPath;
		mesh->tex_materials.push_back(material);

#if PCL_VERSION_COMPARE(>=, 1, 8, 0)
		std::vector<Eigen::Vector2f, Eigen::aligned_allocator<Eigen::Vector2f> > coordinates;
#else
		std::vector<Eigen::Vector2f> coordinates;
#endif
		coordinates.push_back(Eigen::Vector2f(0,1));
		coordinates.push_back(Eigen::Vector2f(1,1));
		coordinates.push_back(Eigen::Vector2f(1,0));
		coordinates.push_back(Eigen::Vector2f(0,0));
		mesh->tex_coordinates.push_back(coordinates);

		_visualizer->addTextureMesh(*mesh, "map");
		_visualizer->getCloudActorMap()->find("map")->second.actor->GetProperty()->LightingOff();
		setCloudOpacity("map", 0.7);

		//removed tmp texture file
		QFile::remove(tmpPath.c_str());
	}
	return true;
#else
	// not implemented on lower version of PCL
	return false;
#endif
}

void CloudViewer::removeOccupancyGridMap()
{
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
	if(_visualizer->getShapeActorMap()->find("map") == _visualizer->getShapeActorMap()->end())
	{
		_visualizer->removeShape("map");
	}
#endif
}

void CloudViewer::addOrUpdateCoordinate(
			const std::string & id,
			const Transform & transform,
			double scale)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	removeCoordinate(id);

	if(!transform.isNull())
	{
		_coordinates.insert(id);
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
		_visualizer->addCoordinateSystem(scale, transform.toEigen3f(), id);
#else
		// Well, on older versions, just update the main coordinate
		_visualizer->addCoordinateSystem(scale, transform.toEigen3f(), 0);
#endif
	}
}

bool CloudViewer::updateCoordinatePose(
		const std::string & id,
		const Transform & pose)
{
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
	if(_coordinates.find(id) != _coordinates.end() && !pose.isNull())
	{
		UDEBUG("Updating pose %s to %s", id.c_str(), pose.prettyPrint().c_str());
		return _visualizer->updateCoordinateSystemPose(id, pose.toEigen3f());
	}
#else
	UERROR("CloudViewer::updateCoordinatePose() is not available on PCL < 1.7.2");
#endif
	return false;
}

void CloudViewer::removeCoordinate(const std::string & id)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	if(_coordinates.find(id) != _coordinates.end())
	{
#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
		_visualizer->removeCoordinateSystem(id);
#else
		// Well, on older versions, just update the main coordinate
		_visualizer->removeCoordinateSystem(0);
#endif
		_coordinates.erase(id);
	}
}

void CloudViewer::removeAllCoordinates()
{
	std::set<std::string> coordinates = _coordinates;
	for(std::set<std::string>::iterator iter = coordinates.begin(); iter!=coordinates.end(); ++iter)
	{
		this->removeCoordinate(*iter);
	}
	UASSERT(_coordinates.empty());
}

static const float frustum_vertices[] = {
    0.0f,  0.0f, 0.0f,
	1.0f, 1.0f, 1.0f,
	1.0f, -1.0f, 1.0f,
	1.0f, -1.0f, -1.0f,
	1.0f, 1.0f, -1.0f};

static const int frustum_indices[] = {
    1, 2, 3, 4, 1, 0, 2, 0, 3, 0, 4};

void CloudViewer::addOrUpdateFrustum(
			const std::string & id,
			const Transform & transform,
			double scale,
			const QColor & color)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	removeFrustum(id);

	if(!transform.isNull())
	{
		_frustums.insert(id);

		int frustumSize = sizeof(frustum_vertices)/sizeof(float);
		UASSERT(frustumSize>0 && frustumSize % 3 == 0);
		frustumSize/=3;
		pcl::PointCloud<pcl::PointXYZ> frustumPoints;
		frustumPoints.resize(frustumSize);
		float scaleX = 0.5f * scale;
		float scaleY = 0.4f * scale; //4x3 arbitrary ratio
		float scaleZ = 0.3f * scale;
		QColor c = Qt::gray;
		if(color.isValid())
		{
			c = color;
		}
		Eigen::Affine3f t = transform.toEigen3f();
		for(int i=0; i<frustumSize; ++i)
		{
			frustumPoints[i].x = frustum_vertices[i*3]*scaleX;
			frustumPoints[i].y = frustum_vertices[i*3+1]*scaleY;
			frustumPoints[i].z = frustum_vertices[i*3+2]*scaleZ;
			frustumPoints[i] = pcl::transformPoint(frustumPoints[i], t);
		}

		pcl::PolygonMesh mesh;
		pcl::Vertices vertices;
		vertices.vertices.resize(sizeof(frustum_indices)/sizeof(int));
		for(unsigned int i=0; i<vertices.vertices.size(); ++i)
		{
			vertices.vertices[i] = frustum_indices[i];
		}
		pcl::toPCLPointCloud2(frustumPoints, mesh.cloud);
		mesh.polygons.push_back(vertices);
		_visualizer->addPolylineFromPolygonMesh(mesh, id);
		_visualizer->setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, c.redF(), c.greenF(), c.blueF(), id);

	}
}

bool CloudViewer::updateFrustumPose(
		const std::string & id,
		const Transform & pose)
{
	if(_frustums.find(id) != _frustums.end() && !pose.isNull())
	{
		UDEBUG("Updating pose %s to %s", id.c_str(), pose.prettyPrint().c_str());
		return _visualizer->updateShapePose(id, pose.toEigen3f());
	}
	return false;
}

void CloudViewer::removeFrustum(const std::string & id)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	if(_frustums.find(id) != _frustums.end())
	{
		_visualizer->removeShape(id);
		_frustums.erase(id);
	}
}

void CloudViewer::removeAllFrustums()
{
	std::set<std::string> frustums = _frustums;
	for(std::set<std::string>::iterator iter = frustums.begin(); iter!=frustums.end(); ++iter)
	{
		this->removeFrustum(*iter);
	}
	UASSERT(_frustums.empty());
}

void CloudViewer::addOrUpdateGraph(
		const std::string & id,
		const pcl::PointCloud<pcl::PointXYZ>::Ptr & graph,
		const QColor & color)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	removeGraph(id);

	if(graph->size())
	{
		_graphes.insert(id);

		pcl::PolygonMesh mesh;
		pcl::Vertices vertices;
		vertices.vertices.resize(graph->size());
		for(unsigned int i=0; i<vertices.vertices.size(); ++i)
		{
			vertices.vertices[i] = i;
		}
		pcl::toPCLPointCloud2(*graph, mesh.cloud);
		mesh.polygons.push_back(vertices);
		_visualizer->addPolylineFromPolygonMesh(mesh, id);
		_visualizer->setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, color.redF(), color.greenF(), color.blueF(), id);

		this->addCloud(id+"_nodes", graph, Transform::getIdentity(), color);
		this->setCloudPointSize(id+"_nodes", 5);
	}
}

void CloudViewer::removeGraph(const std::string & id)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	if(_graphes.find(id) != _graphes.end())
	{
		_visualizer->removeShape(id);
		_graphes.erase(id);
		removeCloud(id+"_nodes");
	}
}

void CloudViewer::removeAllGraphs()
{
	std::set<std::string> graphes = _graphes;
	for(std::set<std::string>::iterator iter = graphes.begin(); iter!=graphes.end(); ++iter)
	{
		this->removeGraph(*iter);
	}
	UASSERT(_graphes.empty());
}

void CloudViewer::addOrUpdateText(
			const std::string & id,
			const std::string & text,
			const Transform & position,
			double scale,
			const QColor & color)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	removeCoordinate(id);

	if(!position.isNull())
	{
		_texts.insert(id);
		_visualizer->addText3D(
				text,
				pcl::PointXYZ(position.x(), position.y(), position.z()),
				scale,
				color.redF(),
				color.greenF(),
				color.blueF(),
				id);
	}
}

void CloudViewer::removeText(const std::string & id)
{
	if(id.empty())
	{
		UERROR("id should not be empty!");
		return;
	}

	if(_texts.find(id) != _texts.end())
	{
		_visualizer->removeText3D(id);
		_texts.erase(id);
	}
}

void CloudViewer::removeAllTexts()
{
	std::set<std::string> texts = _texts;
	for(std::set<std::string>::iterator iter = texts.begin(); iter!=texts.end(); ++iter)
	{
		this->removeText(*iter);
	}
	UASSERT(_texts.empty());
}

bool CloudViewer::isTrajectoryShown() const
{
	return _aShowTrajectory->isChecked();
}

unsigned int CloudViewer::getTrajectorySize() const
{
	return _maxTrajectorySize;
}

void CloudViewer::setTrajectoryShown(bool shown)
{
	_aShowTrajectory->setChecked(shown);
}

void CloudViewer::setTrajectorySize(unsigned int value)
{
	_maxTrajectorySize = value;
}

void CloudViewer::clearTrajectory()
{
	_trajectory->clear();
	_visualizer->removeShape("trajectory");
	this->update();
}

bool CloudViewer::isFrustumShown() const
{
	return _aShowFrustum->isChecked();
}

float CloudViewer::getFrustumScale() const
{
	return _frustumScale;
}

QColor CloudViewer::getFrustumColor() const
{
	return _frustumColor;
}

void CloudViewer::setFrustumShown(bool shown)
{
	if(!shown)
	{
		this->removeFrustum("reference_frustum");
	}
	_aShowFrustum->setChecked(shown);
}

void CloudViewer::setFrustumScale(float value)
{
	_frustumScale = value;
}

void CloudViewer::setFrustumColor(QColor value)
{
	if(!value.isValid())
	{
		value = Qt::gray;
	}
	if(_frustums.find("reference_frustum") != _frustums.end())
	{
		_visualizer->setShapeRenderingProperties(pcl::visualization::PCL_VISUALIZER_COLOR, value.redF(), value.greenF(), value.blueF(), "reference_frustum");
		this->update();
	}
	_frustumColor = value;
}

void CloudViewer::resetCamera()
{
	_lastCameraOrientation= _lastCameraPose = cv::Vec3f(0,0,0);
	if((_aFollowCamera->isChecked() || _aLockCamera->isChecked()) && !_lastPose.isNull())
	{
		// reset relative to last current pose
		if(_aLockViewZ->isChecked())
		{
			_visualizer->setCameraPosition(
					_lastPose.x()-1, _lastPose.y(), _lastPose.z(),
					_lastPose.x(), _lastPose.y(), _lastPose.z(),
					0, 0, 1);
		}
		else
		{
			_visualizer->setCameraPosition(
					_lastPose.x()-1, _lastPose.y(), _lastPose.z(),
					_lastPose.x(), _lastPose.y(), _lastPose.z(),
					_lastPose.r31(), _lastPose.r32(), _lastPose.r33());
		}
	}
	else
	{
		_visualizer->setCameraPosition(
				-1, 0, 0,
				0, 0, 0,
				0, 0, 1);
	}
	this->update();
}

void CloudViewer::removeAllClouds()
{
	_addedClouds.clear();
	_visualizer->removeAllPointClouds();
}


bool CloudViewer::removeCloud(const std::string & id)
{
	bool success = _visualizer->removePointCloud(id);
	_addedClouds.remove(id); // remove after visualizer
	return success;
}

bool CloudViewer::getPose(const std::string & id, Transform & pose)
{
	if(_addedClouds.contains(id))
	{
		pose = _addedClouds.value(id);
		return true;
	}
	return false;
}

Transform CloudViewer::getTargetPose() const
{
	if(_lastPose.isNull())
	{
		return Transform::getIdentity();
	}
	return _lastPose;
}

void CloudViewer::setBackfaceCulling(bool enabled, bool frontfaceCulling)
{
	_backfaceCulling = enabled;
	_frontfaceCulling = frontfaceCulling;
}

void CloudViewer::getCameraPosition(
		float & x, float & y, float & z,
		float & focalX, float & focalY, float & focalZ,
		float & upX, float & upY, float & upZ) const
{
	std::vector<pcl::visualization::Camera> cameras;
	_visualizer->getCameras(cameras);
	if(cameras.size())
	{
		x = cameras.begin()->pos[0];
		y = cameras.begin()->pos[1];
		z = cameras.begin()->pos[2];
		focalX = cameras.begin()->focal[0];
		focalY = cameras.begin()->focal[1];
		focalZ = cameras.begin()->focal[2];
		upX = cameras.begin()->view[0];
		upY = cameras.begin()->view[1];
		upZ = cameras.begin()->view[2];
	}
	else
	{
		UERROR("No camera set!?");
	}
}

void CloudViewer::setCameraPosition(
		float x, float y, float z,
		float focalX, float focalY, float focalZ,
		float upX, float upY, float upZ)
{
	_lastCameraOrientation= _lastCameraPose= cv::Vec3f(0,0,0);
	_visualizer->setCameraPosition(x,y,z, focalX,focalY,focalX, upX,upY,upZ);
}

void CloudViewer::updateCameraTargetPosition(const Transform & pose)
{
	if(!pose.isNull())
	{
		Eigen::Affine3f m = pose.toEigen3f();
		Eigen::Vector3f pos = m.translation();

		Eigen::Vector3f lastPos(0,0,0);
		if(_trajectory->size())
		{
			lastPos[0]=_trajectory->back().x;
			lastPos[1]=_trajectory->back().y;
			lastPos[2]=_trajectory->back().z;
		}

		_trajectory->push_back(pcl::PointXYZ(pos[0], pos[1], pos[2]));
		if(_maxTrajectorySize>0)
		{
			while(_trajectory->size() > _maxTrajectorySize)
			{
				_trajectory->erase(_trajectory->begin());
			}
		}
		if(_aShowTrajectory->isChecked())
		{
			_visualizer->removeShape("trajectory");
			pcl::PolygonMesh mesh;
			pcl::Vertices vertices;
			vertices.vertices.resize(_trajectory->size());
			for(unsigned int i=0; i<vertices.vertices.size(); ++i)
			{
				vertices.vertices[i] = i;
			}
			pcl::toPCLPointCloud2(*_trajectory, mesh.cloud);
			mesh.polygons.push_back(vertices);
			_visualizer->addPolylineFromPolygonMesh(mesh, "trajectory");
		}

		if(pose != _lastPose || _lastPose.isNull())
		{
			if(_lastPose.isNull())
			{
				_lastPose.setIdentity();
			}

			std::vector<pcl::visualization::Camera> cameras;
			_visualizer->getCameras(cameras);

			if(_aLockCamera->isChecked())
			{
				//update camera position
				Eigen::Vector3f diff = pos - Eigen::Vector3f(_lastPose.x(), _lastPose.y(), _lastPose.z());
				cameras.front().pos[0] += diff[0];
				cameras.front().pos[1] += diff[1];
				cameras.front().pos[2] += diff[2];
				cameras.front().focal[0] += diff[0];
				cameras.front().focal[1] += diff[1];
				cameras.front().focal[2] += diff[2];
			}
			else if(_aFollowCamera->isChecked())
			{
				Eigen::Vector3f vPosToFocal = Eigen::Vector3f(cameras.front().focal[0] - cameras.front().pos[0],
															  cameras.front().focal[1] - cameras.front().pos[1],
															  cameras.front().focal[2] - cameras.front().pos[2]).normalized();
				Eigen::Vector3f zAxis(cameras.front().view[0], cameras.front().view[1], cameras.front().view[2]);
				Eigen::Vector3f yAxis = zAxis.cross(vPosToFocal);
				Eigen::Vector3f xAxis = yAxis.cross(zAxis);
				Transform PR(xAxis[0], xAxis[1], xAxis[2],0,
							yAxis[0], yAxis[1], yAxis[2],0,
							zAxis[0], zAxis[1], zAxis[2],0);

				Transform P(PR[0], PR[1], PR[2], cameras.front().pos[0],
							PR[4], PR[5], PR[6], cameras.front().pos[1],
							PR[8], PR[9], PR[10], cameras.front().pos[2]);
				Transform F(PR[0], PR[1], PR[2], cameras.front().focal[0],
							PR[4], PR[5], PR[6], cameras.front().focal[1],
							PR[8], PR[9], PR[10], cameras.front().focal[2]);
				Transform N = pose;
				Transform O = _lastPose;
				Transform O2N = O.inverse()*N;
				Transform F2O = F.inverse()*O;
				Transform T = F2O * O2N * F2O.inverse();
				Transform Fp = F * T;
				Transform P2F = P.inverse()*F;
				Transform Pp = P * P2F * T * P2F.inverse();

				cameras.front().pos[0] = Pp.x();
				cameras.front().pos[1] = Pp.y();
				cameras.front().pos[2] = Pp.z();
				cameras.front().focal[0] = Fp.x();
				cameras.front().focal[1] = Fp.y();
				cameras.front().focal[2] = Fp.z();
				//FIXME: the view up is not set properly...
				cameras.front().view[0] = _aLockViewZ->isChecked()?0:Fp[8];
				cameras.front().view[1] = _aLockViewZ->isChecked()?0:Fp[9];
				cameras.front().view[2] = _aLockViewZ->isChecked()?1:Fp[10];
			}

#if PCL_VERSION_COMPARE(>=, 1, 7, 2)
			if(_coordinates.find("reference") != _coordinates.end())
			{
				this->updateCoordinatePose("reference", pose);
			}
			else
#endif
			{
				this->addOrUpdateCoordinate("reference", pose, 0.2);
			}

			// commented: update pose is crashing...
			/*if(_frustums.find("reference_frustum") != _frustums.end())
			{
				this->updateFrustumPose("reference_frustum", pose);
			}
			else */ if(_aShowFrustum->isChecked())
			{
				this->addOrUpdateFrustum("reference_frustum", pose, _frustumScale, _frustumColor);
			}

			vtkRenderer* renderer = _visualizer->getRendererCollection()->GetFirstRenderer();
			vtkSmartPointer<vtkCamera> cam = renderer->GetActiveCamera ();
			cam->SetPosition (cameras.front().pos[0], cameras.front().pos[1], cameras.front().pos[2]);
			cam->SetFocalPoint (cameras.front().focal[0], cameras.front().focal[1], cameras.front().focal[2]);
			cam->SetViewUp (cameras.front().view[0], cameras.front().view[1], cameras.front().view[2]);
			renderer->ResetCameraClippingRange();
		}
	}

	_lastPose = pose;
}

const QColor & CloudViewer::getDefaultBackgroundColor() const
{
	return _defaultBgColor;
}

void CloudViewer::setDefaultBackgroundColor(const QColor & color)
{
	if(_currentBgColor == _defaultBgColor)
	{
		setBackgroundColor(color);
	}
	_defaultBgColor = color;
}

const QColor & CloudViewer::getBackgroundColor() const
{
	return _currentBgColor;
}

void CloudViewer::setBackgroundColor(const QColor & color)
{
	_currentBgColor = color;
	_visualizer->setBackgroundColor(color.redF(), color.greenF(), color.blueF());
}

void CloudViewer::setCloudVisibility(const std::string & id, bool isVisible)
{
	pcl::visualization::CloudActorMapPtr cloudActorMap = _visualizer->getCloudActorMap();
	pcl::visualization::CloudActorMap::iterator iter = cloudActorMap->find(id);
	if(iter != cloudActorMap->end())
	{
		iter->second.actor->SetVisibility(isVisible?1:0);
	}
	else
	{
		UERROR("Cannot find actor named \"%s\".", id.c_str());
	}
}

bool CloudViewer::getCloudVisibility(const std::string & id)
{
	pcl::visualization::CloudActorMapPtr cloudActorMap = _visualizer->getCloudActorMap();
	pcl::visualization::CloudActorMap::iterator iter = cloudActorMap->find(id);
	if(iter != cloudActorMap->end())
	{
		return iter->second.actor->GetVisibility() != 0;
	}
	else
	{
		UERROR("Cannot find actor named \"%s\".", id.c_str());
	}
	return false;
}

void CloudViewer::setCloudOpacity(const std::string & id, double opacity)
{
	double lastOpacity;
	_visualizer->getPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_OPACITY, lastOpacity, id);
	if(lastOpacity != opacity)
	{
		_visualizer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_OPACITY, opacity, id);
	}
}

void CloudViewer::setCloudPointSize(const std::string & id, int size)
{
	double lastSize;
	_visualizer->getPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, lastSize, id);
	if((int)lastSize != size)
	{
		_visualizer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, (double)size, id);
	}
}

void CloudViewer::setCameraTargetLocked(bool enabled)
{
	_aLockCamera->setChecked(enabled);
}

void CloudViewer::setCameraTargetFollow(bool enabled)
{
	_aFollowCamera->setChecked(enabled);
}

void CloudViewer::setCameraFree()
{
	_aLockCamera->setChecked(false);
	_aFollowCamera->setChecked(false);
}

void CloudViewer::setCameraLockZ(bool enabled)
{
	_lastCameraOrientation= _lastCameraPose = cv::Vec3f(0,0,0);
	_aLockViewZ->setChecked(enabled);
}

void CloudViewer::setGridShown(bool shown)
{
	_aShowGrid->setChecked(shown);
	if(shown)
	{
		this->addGrid();
	}
	else
	{
		this->removeGrid();
	}
}

bool CloudViewer::isCameraTargetLocked() const
{
	return _aLockCamera->isChecked();
}
bool CloudViewer::isCameraTargetFollow() const
{
	return _aFollowCamera->isChecked();
}
bool CloudViewer::isCameraFree() const
{
	return !_aFollowCamera->isChecked() && !_aLockCamera->isChecked();
}
bool CloudViewer::isCameraLockZ() const
{
	return _aLockViewZ->isChecked();
}
bool CloudViewer::isGridShown() const
{
	return _aShowGrid->isChecked();
}
unsigned int CloudViewer::getGridCellCount() const
{
	return _gridCellCount;
}
float CloudViewer::getGridCellSize() const
{
	return _gridCellSize;
}

void CloudViewer::setGridCellCount(unsigned int count)
{
	if(count > 0)
	{
		_gridCellCount = count;
		if(_aShowGrid->isChecked())
		{
			this->removeGrid();
			this->addGrid();
		}
	}
	else
	{
		UERROR("Cannot set grid cell count < 1, count=%d", count);
	}
}
void CloudViewer::setGridCellSize(float size)
{
	if(size > 0)
	{
		_gridCellSize = size;
		if(_aShowGrid->isChecked())
		{
			this->removeGrid();
			this->addGrid();
		}
	}
	else
	{
		UERROR("Cannot set grid cell size <= 0, value=%f", size);
	}
}
void CloudViewer::addGrid()
{
	if(_gridLines.empty())
	{
		float cellSize = _gridCellSize;
		int cellCount = _gridCellCount;
		double r=0.5;
		double g=0.5;
		double b=0.5;
		int id = 0;
		float min = -float(cellCount/2) * cellSize;
		float max = float(cellCount/2) * cellSize;
		std::string name;
		for(float i=min; i<=max; i += cellSize)
		{
			//over x
			name = uFormat("line%d", ++id);
			_visualizer->addLine(pcl::PointXYZ(i, min, 0.0f), pcl::PointXYZ(i, max, 0.0f), r, g, b, name);
			_gridLines.push_back(name);
			//over y or z
			name = uFormat("line%d", ++id);
			_visualizer->addLine(
					pcl::PointXYZ(min, i, 0),
					pcl::PointXYZ(max, i, 0),
					r, g, b, name);
			_gridLines.push_back(name);
		}
	}
}

void CloudViewer::removeGrid()
{
	for(std::list<std::string>::iterator iter = _gridLines.begin(); iter!=_gridLines.end(); ++iter)
	{
		_visualizer->removeShape(*iter);
	}
	_gridLines.clear();
}

Eigen::Vector3f rotatePointAroundAxe(
		const Eigen::Vector3f & point,
		const Eigen::Vector3f & axis,
		float angle)
{
	Eigen::Vector3f direction = point;
	Eigen::Vector3f zAxis = axis;
	float dotProdZ = zAxis.dot(direction);
	Eigen::Vector3f ptOnZaxis = zAxis * dotProdZ;
	direction -= ptOnZaxis;
	Eigen::Vector3f xAxis = direction.normalized();
	Eigen::Vector3f yAxis = zAxis.cross(xAxis);

	Eigen::Matrix3f newFrame;
	newFrame << xAxis[0], yAxis[0], zAxis[0],
				  xAxis[1], yAxis[1], zAxis[1],
				  xAxis[2], yAxis[2], zAxis[2];

	// transform to axe frame
	// transpose=inverse for orthogonal matrices
	Eigen::Vector3f newDirection = newFrame.transpose() * direction;

	// rotate about z
	float cosTheta = cos(angle);
	float sinTheta = sin(angle);
	float magnitude = newDirection.norm();
	newDirection[0] = ( magnitude * cosTheta );
	newDirection[1] = ( magnitude * sinTheta );

	// transform back to global frame
	direction = newFrame * newDirection;

	return direction + ptOnZaxis;
}

void CloudViewer::keyReleaseEvent(QKeyEvent * event) {
	if(event->key() == Qt::Key_Up ||
		event->key() == Qt::Key_Down ||
		event->key() == Qt::Key_Left ||
		event->key() == Qt::Key_Right)
	{
		_keysPressed -= (Qt::Key)event->key();
	}
	else
	{
		QVTKWidget::keyPressEvent(event);
	}
}

void CloudViewer::keyPressEvent(QKeyEvent * event)
{
	if(event->key() == Qt::Key_Up ||
		event->key() == Qt::Key_Down ||
		event->key() == Qt::Key_Left ||
		event->key() == Qt::Key_Right)
	{
		_keysPressed += (Qt::Key)event->key();

		std::vector<pcl::visualization::Camera> cameras;
		_visualizer->getCameras(cameras);

		//update camera position
		Eigen::Vector3f pos(cameras.front().pos[0], cameras.front().pos[1], _aLockViewZ->isChecked()?0:cameras.front().pos[2]);
		Eigen::Vector3f focal(cameras.front().focal[0], cameras.front().focal[1], _aLockViewZ->isChecked()?0:cameras.front().focal[2]);
		Eigen::Vector3f viewUp(cameras.front().view[0], cameras.front().view[1], cameras.front().view[2]);
		Eigen::Vector3f cummulatedDir(0,0,0);
		Eigen::Vector3f cummulatedFocalDir(0,0,0);
		float step = 0.2f;
		float stepRot = 0.02f; // radian
		if(_keysPressed.contains(Qt::Key_Up))
		{
			Eigen::Vector3f dir;
			if(event->modifiers() & Qt::ShiftModifier)
			{
				dir = viewUp * step;// up
			}
			else
			{
				dir = (focal-pos).normalized() * step; // forward
			}
			cummulatedDir += dir;
		}
		if(_keysPressed.contains(Qt::Key_Down))
		{
			Eigen::Vector3f dir;
			if(event->modifiers() & Qt::ShiftModifier)
			{
				dir = viewUp * -step;// down
			}
			else
			{
				dir = (focal-pos).normalized() * -step; // backward
			}
			cummulatedDir += dir;
		}
		if(_keysPressed.contains(Qt::Key_Right))
		{
			if(event->modifiers() & Qt::ShiftModifier)
			{
				// rotate right
				Eigen::Vector3f point = (focal-pos);
				Eigen::Vector3f newPoint = rotatePointAroundAxe(point, viewUp, -stepRot);
				Eigen::Vector3f diff = newPoint - point;
				cummulatedFocalDir += diff;
			}
			else
			{
				Eigen::Vector3f dir = ((focal-pos).cross(viewUp)).normalized() * step; // strafing right
				cummulatedDir += dir;
			}
		}
		if(_keysPressed.contains(Qt::Key_Left))
		{
			if(event->modifiers() & Qt::ShiftModifier)
			{
				// rotate left
				Eigen::Vector3f point = (focal-pos);
				Eigen::Vector3f newPoint = rotatePointAroundAxe(point, viewUp, stepRot);
				Eigen::Vector3f diff = newPoint - point;
				cummulatedFocalDir += diff;
			}
			else
			{
				Eigen::Vector3f dir = ((focal-pos).cross(viewUp)).normalized() * -step; // strafing left
				cummulatedDir += dir;
			}
		}

		cameras.front().pos[0] += cummulatedDir[0];
		cameras.front().pos[1] += cummulatedDir[1];
		cameras.front().pos[2] += cummulatedDir[2];
		cameras.front().focal[0] += cummulatedDir[0] + cummulatedFocalDir[0];
		cameras.front().focal[1] += cummulatedDir[1] + cummulatedFocalDir[1];
		cameras.front().focal[2] += cummulatedDir[2] + cummulatedFocalDir[2];
		_visualizer->setCameraPosition(
			cameras.front().pos[0], cameras.front().pos[1], cameras.front().pos[2],
			cameras.front().focal[0], cameras.front().focal[1], cameras.front().focal[2],
			cameras.front().view[0], cameras.front().view[1], cameras.front().view[2]);

		update();

		emit configChanged();
	}
	else
	{
		QVTKWidget::keyPressEvent(event);
	}
}

void CloudViewer::mousePressEvent(QMouseEvent * event)
{
	if(event->button() == Qt::RightButton)
	{
		event->accept();
	}
	else
	{
		QVTKWidget::mousePressEvent(event);
	}
}

void CloudViewer::mouseMoveEvent(QMouseEvent * event)
{
	QVTKWidget::mouseMoveEvent(event);

	// camera view up z locked?
	if(_aLockViewZ->isChecked())
	{
		std::vector<pcl::visualization::Camera> cameras;
		_visualizer->getCameras(cameras);

		cv::Vec3d newCameraOrientation = cv::Vec3d(0,0,1).cross(cv::Vec3d(cameras.front().pos)-cv::Vec3d(cameras.front().focal));

		if(	_lastCameraOrientation!=cv::Vec3d(0,0,0) &&
			_lastCameraPose!=cv::Vec3d(0,0,0) &&
			(uSign(_lastCameraOrientation[0]) != uSign(newCameraOrientation[0]) &&
			 uSign(_lastCameraOrientation[1]) != uSign(newCameraOrientation[1])))
		{
			cameras.front().pos[0] = _lastCameraPose[0];
			cameras.front().pos[1] = _lastCameraPose[1];
			cameras.front().pos[2] = _lastCameraPose[2];
		}
		else if(newCameraOrientation != cv::Vec3d(0,0,0))
		{
			_lastCameraOrientation = newCameraOrientation;
			_lastCameraPose = cv::Vec3d(cameras.front().pos);
		}
		cameras.front().view[0] = 0;
		cameras.front().view[1] = 0;
		cameras.front().view[2] = 1;

		_visualizer->setCameraPosition(
			cameras.front().pos[0], cameras.front().pos[1], cameras.front().pos[2],
			cameras.front().focal[0], cameras.front().focal[1], cameras.front().focal[2],
			cameras.front().view[0], cameras.front().view[1], cameras.front().view[2]);

	}
	this->update();

	emit configChanged();
}

void CloudViewer::wheelEvent(QWheelEvent * event)
{
	QVTKWidget::wheelEvent(event);
	if(_aLockViewZ->isChecked())
	{
		std::vector<pcl::visualization::Camera> cameras;
		_visualizer->getCameras(cameras);
		_lastCameraPose = cv::Vec3d(cameras.front().pos);
	}
	emit configChanged();
}

void CloudViewer::contextMenuEvent(QContextMenuEvent * event)
{
	QAction * a = _menu->exec(event->globalPos());
	if(a)
	{
		handleAction(a);
		emit configChanged();
	}
}

void CloudViewer::handleAction(QAction * a)
{
	if(a == _aSetTrajectorySize)
	{
		bool ok;
		int value = QInputDialog::getInt(this, tr("Set trajectory size"), tr("Size (0=infinite)"), _maxTrajectorySize, 0, 10000, 10, &ok);
		if(ok)
		{
			_maxTrajectorySize = value;
		}
	}
	else if(a == _aClearTrajectory)
	{
		this->clearTrajectory();
	}
	else if(a == _aShowFrustum)
	{
		this->setFrustumShown(a->isChecked());
	}
	else if(a == _aSetFrustumScale)
	{
		bool ok;
		double value = QInputDialog::getDouble(this, tr("Set frustum scale"), tr("Scale"), _frustumScale, 0.0, 999.0, 1, &ok);
		if(ok)
		{
			this->setFrustumScale(value);
		}
	}
	else if(a == _aSetFrustumColor)
	{
		QColor value = QColorDialog::getColor(_frustumColor, this);
		if(value.isValid())
		{
			this->setFrustumColor(value);
		}
	}
	else if(a == _aResetCamera)
	{
		this->resetCamera();
	}
	else if(a == _aShowGrid)
	{
		if(_aShowGrid->isChecked())
		{
			this->addGrid();
		}
		else
		{
			this->removeGrid();
		}

		this->update();
	}
	else if(a == _aSetGridCellCount)
	{
		bool ok;
		int value = QInputDialog::getInt(this, tr("Set grid cell count"), tr("Count"), _gridCellCount, 1, 10000, 10, &ok);
		if(ok)
		{
			this->setGridCellCount(value);
		}
	}
	else if(a == _aSetGridCellSize)
	{
		bool ok;
		double value = QInputDialog::getDouble(this, tr("Set grid cell size"), tr("Size (m)"), _gridCellSize, 0.01, 10, 2, &ok);
		if(ok)
		{
			this->setGridCellSize(value);
		}
	}
	else if(a == _aSetBackgroundColor)
	{
		QColor color = this->getDefaultBackgroundColor();
		color = QColorDialog::getColor(color, this);
		if(color.isValid())
		{
			this->setDefaultBackgroundColor(color);
			this->update();
		}
	}
	else if(a == _aLockViewZ)
	{
		if(_aLockViewZ->isChecked())
		{
			this->update();
		}
	}
}

} /* namespace rtabmap */
