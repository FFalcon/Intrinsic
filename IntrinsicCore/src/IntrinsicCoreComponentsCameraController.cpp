// Copyright 2016 Benjamin Glatzel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Precompiled header file
#include "stdafx.h"

// PhysX includes
#include "PxScene.h"

namespace Intrinsic
{
namespace Core
{
namespace Components
{
void CameraControllerManager::init()
{
  _INTR_LOG_INFO("Inititializing Camera Controller Component Manager...");

  Dod::Components::ComponentManagerBase<
      CameraControllerData,
      _INTR_MAX_CAMERA_CONTROLLER_COMPONENT_COUNT>::_initComponentManager();

  Dod::Components::ComponentManagerEntry cameraCtrlEntry;
  {
    cameraCtrlEntry.createFunction =
        Components::CameraControllerManager::createCameraController;
    cameraCtrlEntry.destroyFunction =
        Components::CameraControllerManager::destroyCameraController;
    cameraCtrlEntry.getComponentForEntityFunction =
        Components::CameraControllerManager::getComponentForEntity;
    cameraCtrlEntry.resetToDefaultFunction =
        Components::CameraControllerManager::resetToDefault;

    Application::_componentManagerMapping[_N(CameraController)] =
        cameraCtrlEntry;
    Application::_orderedComponentManagers.push_back(cameraCtrlEntry);
  }

  Dod::PropertyCompilerEntry propCompilerCameraController;
  {
    propCompilerCameraController.compileFunction =
        Components::CameraControllerManager::compileDescriptor;
    propCompilerCameraController.initFunction =
        Components::CameraControllerManager::initFromDescriptor;
    propCompilerCameraController.ref = Dod::Ref();
    Application::_componentPropertyCompilerMapping[_N(CameraController)] =
        propCompilerCameraController;
  }
}

// <-

void updateThirdPersonCamera(CameraControllerRef p_Ref, float p_DeltaT)
{
  const Name& targetObjectName =
      CameraControllerManager::_descTargetObjectName(p_Ref);
  glm::vec3& targetEulerAngles =
      CameraControllerManager::_descTargetEulerAngles(p_Ref);

  // Clamp vertical camera movement
  targetEulerAngles.x = glm::clamp(targetEulerAngles.x, -glm::half_pi<float>(),
                                   glm::half_pi<float>());

  const glm::quat targetOrientation = glm::quat(targetEulerAngles);
  const glm::vec3 targetVector =
      targetOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

  NodeRef cameraNodeRef = NodeManager::getComponentForEntity(
      CameraControllerManager::_entity(p_Ref));
  CameraRef cameraRef = CameraManager::getComponentForEntity(
      CameraControllerManager::_entity(p_Ref));

  glm::vec3& camPosition = NodeManager::_position(cameraNodeRef);
  glm::vec3& camEulerAngles = CameraManager::_descEulerAngles(cameraRef);

  const float camDistance = 10.0f;
  const glm::vec3 localTargetPosition = camDistance * targetVector;
  glm::vec3 worldCamTargetPosition = localTargetPosition;
  glm::vec3 worldTargetPosition = glm::vec3(0.0f);

  if (targetObjectName.isValid())
  {
    Entity::EntityRef targetEntityRef =
        Entity::EntityManager::getEntityByName(targetObjectName);

    if (targetEntityRef.isValid())
    {
      Components::NodeRef targetNodeRef =
          NodeManager::getComponentForEntity(targetEntityRef);

      if (targetNodeRef.isValid())
      {
        worldTargetPosition = NodeManager::_worldPosition(targetNodeRef);
        worldCamTargetPosition += worldTargetPosition;
      }
    }
  }

  // Camera collision
  static const float minOffsetDist = 2.0f;

  const glm::vec3 camOut =
      glm::normalize(worldTargetPosition - worldCamTargetPosition);
  const glm::vec3 nearestCamPos = worldTargetPosition;
  float minHitDistance = FLT_MAX;

  const Math::FrustumCorners& frustumCornersVS =
      Resources::FrustumManager::_frustumCornersViewSpace(
          CameraManager::_frustum(cameraRef));
  for (uint32_t i = 0u; i < 4u; ++i)
  {
    const glm::vec3 corner =
        worldCamTargetPosition + (targetOrientation * frustumCornersVS.c[i]);

    const glm::vec3 offsetToCorner = corner - worldCamTargetPosition;
    const glm::vec3 rayStart = nearestCamPos + offsetToCorner;
    const glm::vec3 rayEnd = corner;

    const Math::Ray rayForward = {rayStart, glm::normalize(rayEnd - rayStart)};

    physx::PxRaycastHit hit;
    if (PhysxHelper::raycast(rayForward, hit, glm::length(rayEnd - rayStart)))
    {
      minHitDistance = glm::min(hit.distance, minHitDistance);
    }
  }

  if (minHitDistance != FLT_MAX)
  {
    minHitDistance = glm::max(minHitDistance, minOffsetDist);
    worldCamTargetPosition = nearestCamPos - camOut * minHitDistance;
  }

  static const float rotationSpeed = 4.0f;
  static const float movementSpeed = 4.0f;

  camEulerAngles =
      glm::mix(camEulerAngles, targetEulerAngles, rotationSpeed * p_DeltaT);
  camPosition =
      glm::mix(camPosition, worldCamTargetPosition, movementSpeed * p_DeltaT);

  NodeManager::updateTransforms(cameraNodeRef);
}

// <-

void updateFirstPersonCamera(CameraControllerRef p_Ref, float p_DeltaT)
{
  const Name& targetObjectName =
      CameraControllerManager::_descTargetObjectName(p_Ref);
  glm::vec3& targetEulerAngles =
      CameraControllerManager::_descTargetEulerAngles(p_Ref);

  // Clamp vertical camera movement
  targetEulerAngles.x = glm::clamp(targetEulerAngles.x, -glm::half_pi<float>(),
                                   glm::half_pi<float>());

  NodeRef cameraNodeRef = NodeManager::getComponentForEntity(
      CameraControllerManager::_entity(p_Ref));
  CameraRef cameraRef = CameraManager::getComponentForEntity(
      CameraControllerManager::_entity(p_Ref));

  glm::vec3& camPosition = NodeManager::_position(cameraNodeRef);
  glm::vec3& camEulerAngles = CameraManager::_descEulerAngles(cameraRef);

  if (targetObjectName.isValid())
  {
    Entity::EntityRef targetEntityRef =
        Entity::EntityManager::getEntityByName(targetObjectName);

    if (targetEntityRef.isValid())
    {
      Components::NodeRef targetNodeRef =
          NodeManager::getComponentForEntity(targetEntityRef);

      if (targetNodeRef.isValid())
      {
        camPosition = NodeManager::_worldPosition(targetNodeRef) +
                      glm::vec3(0.0f, 1.5f, 0.0f);
      }
    }
  }

  camEulerAngles = targetEulerAngles;

  NodeManager::updateTransforms(cameraNodeRef);
}

// <-

void CameraControllerManager::updateControllers(
    const CameraControllerRefArray& p_CamControllers, float p_DeltaT)
{
  for (uint32_t i = 0u; i < p_CamControllers.size(); ++i)
  {
    CameraControllerRef camControllerRef = p_CamControllers[i];

    switch (_descCameraControllerType(camControllerRef))
    {
    case CameraControllerType::kFirstPerson:
      updateFirstPersonCamera(camControllerRef, p_DeltaT);
      break;
    case CameraControllerType::kThirdPerson:
      updateThirdPersonCamera(camControllerRef, p_DeltaT);
      break;
    }
  }
}
}
}
}
