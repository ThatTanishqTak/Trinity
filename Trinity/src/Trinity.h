#pragma once

// ------------BASE LIBRARIES--------------------------------
#include "Trinity/Core/Application.h"
#include "Trinity/Core/Layer.h"
#include "Trinity/Core/Log.h"
#include "Trinity/Core/Timestep.h"
#include "Trinity/Core/Input.h"
#include "Trinity/Core/KeyCodes.h"
#include "Trinity/Core/MouseCodes.h"
// ----------------------------------------------------------

// ------------EXTERNAL LIBRARIES----------------------------
#include "Trinity/ImGui/ImGuiLayer.h"
#include <glm/glm.hpp>
// ----------------------------------------------------------

// ------------RENDERER--------------------------------------
#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Renderer/Renderer2D.h"
#include "Trinity/Renderer/RenderCommand.h"

#include "Trinity/Renderer/Buffer.h"
#include "Trinity/Renderer/Shader.h"
#include "Trinity/Renderer/Framebuffer.h"
#include "Trinity/Renderer/Texture.h"
#include "Trinity/Renderer/SubTexture2D.h"
#include "Trinity/Renderer/VertexArray.h"
#include "Trinity/Renderer/OrthographicCamera.h"
#include "Trinity/Renderer/OrthographicCameraController.h"
// ----------------------------------------------------------

// ------------ENTITY CONPONENT SYSTEM-----------------------
#include "Trinity/Scene/Scene.h"
#include "Trinity/Scene/Components.h"
#include "Trinity/Scene/Entity.h"
#include "Trinity/Scene/ScriptableEntity.h"
// ----------------------------------------------------------