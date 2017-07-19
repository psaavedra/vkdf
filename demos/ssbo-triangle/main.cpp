#include "vkdf.hpp"

// ----------------------------------------------------------------------------
// Renders a triangle on an offscreen image. The shader stores the first
// component of the color on a ssbo. Compares both to check that they are the
// same. Then render the same triangle again, so it can be presented on
// screen.
//
// The reason of rendering the same scene twice is that we want the offscreen
// rendered image to have the same 32-bit format that the ssbo. But that
// format is not compatible with a presentation framebuffer.
//
// Note that the only reason to keep rendering the triangle on screen is
// debugging. One alternative would be remove that, and just keep the ofscreen
// vs ssbo comparison.
//
// ----------------------------------------------------------------------------

#include <inttypes.h>  /* for PRIu64 macro */

#define WIDTH 1024
#define HEIGHT 768

#define SSBO_BINDING 3
#define UBO_BINDING 0
#define VERTEX_INPUT_LOCATION 0

//Reference value is the initial value for both ssbo and the image. So the
//value for not-rendered pixels. Note that in the 16bit case it will be
//converted. This is not really important as far as the ssbo and the image are
//using the same reference value.
#define REFERENCE_VALUE 0.2f
//Converted value, useful for comparison.
static ushort USHORT_REFERENCE_VALUE = REFERENCE_VALUE;

//NOTE: right now, the number of components are hardcoded. One possible
//       improvement would be being able to configure it.
#define DEFAULT_SSBO_NUM_COMPONENTS 1
#define DEFAULT_IMAGE_NUM_COMPONENTS 4

#define DEFAULT_BITS 32

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer offscreen_cmd_buf;
   VkCommandBuffer *onscreen_cmd_bufs;
   VkdfBuffer vertex_buf;
   VkdfImage color_image;
   VkdfBuffer ubo;
   VkRenderPass offscreen_render_pass;
   VkRenderPass onscreen_render_pass;
   VkSemaphore offscreen_draw_sem;

   VkDescriptorSetLayout set_layout_ubo; //used only for the mvp
   VkDescriptorSetLayout set_layout_ssbo;

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline_offscreen;
   VkPipeline pipeline_onscreen;
   VkShaderModule vs_module;
   VkShaderModule fs_module;
   VkFramebuffer framebuffer;
   VkFramebuffer *onscreen_framebuffers;

   VkDescriptorPool descriptor_pool_ubo;
   VkDescriptorSet descriptor_set_ubo;

   glm::mat4 clip;
   glm::mat4 view;
   glm::mat4 projection;
   glm::mat4 mvp;

   //SSBO resources
   VkdfBuffer ssbo;
   VkDescriptorPool descriptor_pool_ssbo;
   VkDescriptorSet descriptor_set_ssbo;

   unsigned bits; //bits per component. Selected by the user
   unsigned bytes;//size in bytes for each component. Derived from bits. Utility sugar.

   unsigned num_ssbo_components; //Num of ssbo components per sample (in this case num_samples == num_pixels)
   unsigned num_ssbo_elements; //Represents the number of individual values
   VkDeviceSize ssbo_size; //Total size in bytes

   unsigned num_image_components;
   unsigned num_image_elements;
   VkDeviceSize image_size;

   unsigned num_pixels;

   void *ssbo_feedback; //utility array used to get back the content of the ssbo
   void *image_feedback; //ditto, but for the image

} DemoResources;

typedef struct {
   glm::vec4 pos;
} VertexData;

static VkdfBuffer
create_vertex_buffer(VkdfContext *ctx)
{
   const VertexData vertex_data[3] = {
      glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f),
      glm::vec4( 1.0f, -1.0f, 0.0f, 1.0f),
      glm::vec4( 0.0f,  1.0f, 0.0f, 1.0f),
   };

   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flag
                         sizeof(vertex_data),                  // size
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,    // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   vkdf_buffer_map_and_fill(ctx, buf, 0, sizeof(vertex_data), vertex_data);

   return buf;
}

static VkdfBuffer
create_ubo(VkdfContext *ctx, glm::mat4 mvp)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         sizeof(mvp),                          // size
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,   // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type
   return buf;
}

static VkFormat
get_offscreen_format(DemoResources *res)
{
   return res->bits == 32 ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
}

static VkRenderPass
create_offscreen_render_pass(VkdfContext *ctx,
                             DemoResources *resources)
{
   VkAttachmentDescription attachments[1];

   // Single color attachment
   attachments[0].format = get_offscreen_format(resources);
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
   attachments[0].flags = 0;

   // Single subpass
   VkAttachmentReference color_reference;
   color_reference.attachment = 0;
   color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkSubpassDescription subpass;
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.flags = 0;
   subpass.inputAttachmentCount = 0;
   subpass.pInputAttachments = NULL;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_reference;
   subpass.pResolveAttachments = NULL;
   subpass.pDepthStencilAttachment = NULL;
   subpass.preserveAttachmentCount = 0;
   subpass.pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 1;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VkResult res =
      vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create render pass");

   return render_pass;
}

static VkRenderPass
create_onscreen_render_pass(VkdfContext *ctx)
{
   VkAttachmentDescription attachments[1];

   // Single color attachment
   attachments[0].format = ctx->surface_format;
   attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
   attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
   attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   attachments[0].flags = 0;

   // Single subpass
   VkAttachmentReference color_reference;
   color_reference.attachment = 0;
   color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   VkSubpassDescription subpass;
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.flags = 0;
   subpass.inputAttachmentCount = 0;
   subpass.pInputAttachments = NULL;
   subpass.colorAttachmentCount = 1;
   subpass.pColorAttachments = &color_reference;
   subpass.pResolveAttachments = NULL;
   subpass.pDepthStencilAttachment = NULL;
   subpass.preserveAttachmentCount = 0;
   subpass.pPreserveAttachments = NULL;

   // Create render pass
   VkRenderPassCreateInfo rp_info;
   rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rp_info.pNext = NULL;
   rp_info.attachmentCount = 1;
   rp_info.pAttachments = attachments;
   rp_info.subpassCount = 1;
   rp_info.pSubpasses = &subpass;
   rp_info.dependencyCount = 0;
   rp_info.pDependencies = NULL;
   rp_info.flags = 0;

   VkRenderPass render_pass;
   VkResult res =
      vkCreateRenderPass(ctx->device, &rp_info, NULL, &render_pass);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create render pass");

   return render_pass;
}

static void
offscreen_pass_commands(VkdfContext *ctx, DemoResources *res)
{
   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->offscreen_render_pass;
   rp_begin.framebuffer = res->framebuffer;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 0;
   rp_begin.pClearValues = NULL;

   vkCmdBeginRenderPass(res->offscreen_cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Pipeline
   vkCmdBindPipeline(res->offscreen_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline_offscreen);

   // Descriptor set
   VkDescriptorSet descriptor_sets[] = {
      res->descriptor_set_ssbo,
      res->descriptor_set_ubo
   };

   vkCmdBindDescriptorSets(res->offscreen_cmd_buf,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                      // First decriptor set
                           2,                      // Descriptor set count
                           descriptor_sets,   // Descriptor sets
                           0,                      // Dynamic offset count
                           NULL);                  // Dynamic offsets

   // Vertex buffer
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->offscreen_cmd_buf,
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &res->vertex_buf.buf,    // Buffers
                          offsets);                // Offsets

   // Viewport and Scissor
   VkViewport viewport;
   viewport.height = ctx->height;
   viewport.width = ctx->width;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->offscreen_cmd_buf, 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = ctx->width;
   scissor.extent.height = ctx->height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->offscreen_cmd_buf, 0, 1, &scissor);

   // Draw
   vkCmdDraw(res->offscreen_cmd_buf,
             3,                    // vertex count
             1,                    // instance count
             0,                    // first vertex
             0);                   // first instance

   vkCmdEndRenderPass(res->offscreen_cmd_buf);
}

// We draw the same scene, but for presentation. Not really connected with the
// offscreen rendering.
static void
onscreen_pass_commands(VkdfContext *ctx, DemoResources *res, uint32_t index)
{
   VkClearValue clear_values[1];
   clear_values[0].color.float32[0] = 0.0f;
   clear_values[0].color.float32[1] = 0.0f;
   clear_values[0].color.float32[2] = 1.0f;
   clear_values[0].color.float32[3] = 1.0f;

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->onscreen_render_pass;
   rp_begin.framebuffer = res->onscreen_framebuffers[index];
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->onscreen_cmd_bufs[index],
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   // Pipeline
   vkCmdBindPipeline(res->onscreen_cmd_bufs[index], VK_PIPELINE_BIND_POINT_GRAPHICS,
                     res->pipeline_onscreen);

   // Descriptor set
   VkDescriptorSet descriptor_sets[] = {
      res->descriptor_set_ssbo,
      res->descriptor_set_ubo
   };

   vkCmdBindDescriptorSets(res->onscreen_cmd_bufs[index],
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           res->pipeline_layout,
                           0,                      // First decriptor set
                           2,                      // Descriptor set count
                           descriptor_sets,   // Descriptor sets
                           0,                      // Dynamic offset count
                           NULL);                  // Dynamic offsets

   // Vertex buffer
   const VkDeviceSize offsets[1] = { 0 };
   vkCmdBindVertexBuffers(res->onscreen_cmd_bufs[index],
                          0,                       // Start Binding
                          1,                       // Binding Count
                          &res->vertex_buf.buf,    // Buffers
                          offsets);                // Offsets

   // Viewport and Scissor
   VkViewport viewport;
   viewport.height = ctx->height;
   viewport.width = ctx->width;
   viewport.minDepth = 0.0f;
   viewport.maxDepth = 1.0f;
   viewport.x = 0;
   viewport.y = 0;
   vkCmdSetViewport(res->onscreen_cmd_bufs[index], 0, 1, &viewport);

   VkRect2D scissor;
   scissor.extent.width = ctx->width;
   scissor.extent.height = ctx->height;
   scissor.offset.x = 0;
   scissor.offset.y = 0;
   vkCmdSetScissor(res->onscreen_cmd_bufs[index], 0, 1, &scissor);

   // Draw
   vkCmdDraw(res->onscreen_cmd_bufs[index],
             3,                    // vertex count
             1,                    // instance count
             0,                    // first vertex
             0);                   // first instance

   vkCmdEndRenderPass(res->onscreen_cmd_bufs[index]);
}


static VkPipelineLayout
create_pipeline_layout(VkdfContext *ctx,
                       DemoResources *resources)
{
   VkPipelineLayoutCreateInfo pipeline_layout_info;
   VkDescriptorSetLayout layouts[] = {
      resources->set_layout_ssbo,
      resources->set_layout_ubo
   };

   pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   pipeline_layout_info.pNext = NULL;
   pipeline_layout_info.pushConstantRangeCount = 0;
   pipeline_layout_info.pPushConstantRanges = NULL;
   pipeline_layout_info.setLayoutCount = 2;
   pipeline_layout_info.pSetLayouts = layouts;
   pipeline_layout_info.flags = 0;

   VkPipelineLayout pipeline_layout;
   VkResult res = vkCreatePipelineLayout(ctx->device,
                                         &pipeline_layout_info,
                                         NULL,
                                         &pipeline_layout);
   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to create pipeline layout");

   return pipeline_layout;
}

static VkDescriptorSet
create_descriptor_set(VkdfContext *ctx,
                      VkDescriptorPool pool,
                      VkDescriptorSetLayout layout)
{
   VkDescriptorSet set;
   VkDescriptorSetAllocateInfo alloc_info[1];
   alloc_info[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   alloc_info[0].pNext = NULL;
   alloc_info[0].descriptorPool = pool;
   alloc_info[0].descriptorSetCount = 1;
   alloc_info[0].pSetLayouts = &layout;
   VkResult res = vkAllocateDescriptorSets(ctx->device, alloc_info, &set);

   if (res != VK_SUCCESS)
      vkdf_fatal("Failed to allocate descriptor set");

   return set;
}

static void
init_matrices(DemoResources *res)
{
   res->clip = glm::mat4(1.0f,  0.0f, 0.0f, 0.0f,
                         0.0f, -1.0f, 0.0f, 0.0f,
                         0.0f,  0.0f, 0.5f, 0.0f,
                         0.0f,  0.0f, 0.5f, 1.0f);

   res->projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

   res->view = glm::lookAt(glm::vec3( 0,  0, -5),  // Camera position
                           glm::vec3( 0,  0,  0),  // Looking at origin
                           glm::vec3( 0,  1,  0)); // Head is up
}

static VkdfBuffer
create_ssbo(VkdfContext *ctx, DemoResources *res)
{
   VkdfBuffer buf =
      vkdf_create_buffer(ctx,
                         0,                                    // flags
                         res->ssbo_size,                     // size
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,   // usage
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memory type

   /* As we are using res->feedback to get back the ssbo content, we also
    * create it here, and use it to fill the ssbo with some initial values.
    */
   res->ssbo_feedback = malloc(res->ssbo_size);

   // Probably we could do this with a memset
   for (unsigned int i = 0; i < res->num_ssbo_elements; i++) {
      if (res->bits == 32)
         ((float *)res->ssbo_feedback)[i] = REFERENCE_VALUE;
      else
         ((ushort *)res->ssbo_feedback)[i] = REFERENCE_VALUE;
   }

   vkdf_buffer_map_and_fill(ctx, buf, 0, res->ssbo_size, res->ssbo_feedback);

   return buf;
}

static VkdfImage
create_color_image(VkdfContext *ctx, DemoResources *res)
{
   VkdfImage image =
      vkdf_create_image_detailed(ctx,
                                 ctx->width,
                                 ctx->height,
                                 1,
                                 VK_IMAGE_TYPE_2D,
                                 get_offscreen_format(res),
                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, // HOST_VISIBLE as we are going to map their content
                                 VK_IMAGE_ASPECT_COLOR_BIT,
                                 VK_IMAGE_VIEW_TYPE_2D,
                                 VK_IMAGE_LAYOUT_PREINITIALIZED); //We want to set the initial value manually.

   res->image_feedback = malloc(res->image_size);

   // Probably we could do this with a memset
   for (unsigned int i = 0; i < res->num_image_elements; i++) {
      if (res->bits == 32)
         ((float *)res->image_feedback)[i] = REFERENCE_VALUE;
      else
         ((ushort *)res->image_feedback)[i] = REFERENCE_VALUE;
   }

   vkdf_image_map_and_fill(ctx, image, 0, res->image_size, res->image_feedback);

   return image;
}

static void
init_resources(VkdfContext *ctx,
               DemoResources *res,
               unsigned bits)
{
   char filename[100];

   assert(bits == 16 || bits == 32);

   memset(res, 0, sizeof(DemoResources));

   res->bits = bits;
   res->bytes = bits / 8;
   res->num_pixels = WIDTH * HEIGHT;

   // SSBO
   res->num_ssbo_components = DEFAULT_SSBO_NUM_COMPONENTS;
   res->num_ssbo_elements = res->num_pixels * res->num_ssbo_components;
   res->ssbo_size = res->num_ssbo_elements * res->bytes;
   res->ssbo = create_ssbo(ctx, res);

   // Compute View, Projection and Cliip matrices
   init_matrices(res);

   // Vertex buffer
   res->vertex_buf = create_vertex_buffer(ctx);

   // UBO (for MVP matrix)
   res->ubo = create_ubo(ctx, res->mvp);

   // Shaders
   res->vs_module = vkdf_create_shader_module(ctx, "shader.vert.spv");
   snprintf(filename, 100, "shader_%ibit.frag.spv", res->bits);
   res->fs_module = vkdf_create_shader_module(ctx, filename);

   // Render pass
   res->offscreen_render_pass = create_offscreen_render_pass(ctx, res);
   res->onscreen_render_pass = create_onscreen_render_pass(ctx);

   // Color image used as offscreen rendering target. We will draw to this
   // image and then compare their content with the ssbo content.
   res->num_image_components = DEFAULT_IMAGE_NUM_COMPONENTS;
   res->num_image_elements = res->num_pixels * res->num_image_components;
   res->image_size = res->num_image_elements * res->bytes;
   res->color_image = create_color_image(ctx, res);

   // Framebuffers

   //Offscreen framebuffer
   res->framebuffer = vkdf_create_framebuffer(ctx,
                                              res->offscreen_render_pass,
                                              res->color_image.view,
                                              ctx->width,
                                              ctx->height,
                                              0, NULL);

   // Framebuffers
   res->onscreen_framebuffers =
      vkdf_create_framebuffers_for_swap_chain(ctx, res->onscreen_render_pass, 0, NULL);

   // Descriptor pools
   res->descriptor_pool_ubo =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

   res->descriptor_pool_ssbo =
      vkdf_create_descriptor_pool(ctx, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);

   // Descriptor sets
   res->set_layout_ubo =
      vkdf_create_buffer_descriptor_set_layout(ctx, UBO_BINDING, 1,
                                               VK_SHADER_STAGE_VERTEX_BIT,
                                               VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
   res->descriptor_set_ubo =
      create_descriptor_set(ctx, res->descriptor_pool_ubo, res->set_layout_ubo);

   res->set_layout_ssbo =
      vkdf_create_buffer_descriptor_set_layout(ctx, SSBO_BINDING, 1,
                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

   res->descriptor_set_ssbo =
      create_descriptor_set(ctx, res->descriptor_pool_ssbo, res->set_layout_ssbo);

   VkDeviceSize ubo_offset = 0;
   VkDeviceSize ubo_size = sizeof(res->mvp);
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set_ubo, res->ubo.buf,
                                     UBO_BINDING, 1, &ubo_offset, &ubo_size,
                                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

   VkDeviceSize ssbo_offset = 0;
   VkDeviceSize ssbo_size = res->ssbo_size;
   vkdf_descriptor_set_buffer_update(ctx, res->descriptor_set_ssbo, res->ssbo.buf,
                                     SSBO_BINDING, 1, &ssbo_offset, &ssbo_size,
                                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

   // Pipeline
   res->pipeline_layout = create_pipeline_layout(ctx, res);

   VkVertexInputBindingDescription vi_binding;
   vi_binding.binding = 0;
   vi_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   vi_binding.stride = sizeof(VertexData);

   VkVertexInputAttributeDescription vi_attribs[1];
   vi_attribs[0].binding = 0;
   vi_attribs[0].location = VERTEX_INPUT_LOCATION;
   vi_attribs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
   vi_attribs[0].offset = 0;

   res->pipeline_offscreen = vkdf_create_gfx_pipeline(ctx,
                                            NULL,
                                            1,
                                            &vi_binding,
                                            1,
                                            vi_attribs,
                                            false,
                                            res->offscreen_render_pass,
                                            res->pipeline_layout,
                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                            VK_CULL_MODE_NONE,
                                            res->vs_module,
                                            res->fs_module);

   res->pipeline_onscreen = vkdf_create_gfx_pipeline(ctx,
                                                     NULL,
                                                     1,
                                                     &vi_binding,
                                                     1,
                                                     vi_attribs,
                                                     false,
                                                     res->onscreen_render_pass,
                                                     res->pipeline_layout,
                                                     VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                     VK_CULL_MODE_NONE,
                                                     res->vs_module,
                                                     res->fs_module);

   // Command pool
   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);

   // Command buffer for offscreen rendering. A single command buffer that
   // renders the scene to the offscreen image.
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1,
                              &res->offscreen_cmd_buf);
   vkdf_command_buffer_begin(res->offscreen_cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
   offscreen_pass_commands(ctx, res);
   vkdf_command_buffer_end(res->offscreen_cmd_buf);

   // Command buffers for presentation. A command buffer for each swap chain
   // image that copies the offscreen image contents to the corresponding
   // swap chain image.
   res->onscreen_cmd_bufs = g_new(VkCommandBuffer, ctx->swap_chain_length);
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              ctx->swap_chain_length,
                              res->onscreen_cmd_bufs);

   for (uint32_t i = 0; i < ctx->swap_chain_length; i++) {
      vkdf_command_buffer_begin(res->onscreen_cmd_bufs[i],
                                VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
      onscreen_pass_commands(ctx, res, i);
      vkdf_command_buffer_end(res->onscreen_cmd_bufs[i]);
   }

   // Offscreen rendering semaphore. We need this to synchronize the command
   // buffer that renders to the offscreen image and the command buffer that
   // copies from the offscreen image to the presentation image.
   res->offscreen_draw_sem = vkdf_create_semaphore(ctx);
}

static void
update_mvp(DemoResources *res)
{
   //Note: sets the same value
   float rotX = 0.0f;
   float rotY = 0.0f;
   float rotZ = 0.0f;

   rotY += 0.005f;
   rotX += 0.007f;
   rotZ += 0.009f;

   glm::mat4 Model(1.0f);
   Model = glm::rotate(Model, rotX, glm::vec3(1, 0, 0));
   Model = glm::rotate(Model, rotY, glm::vec3(0, 1, 0));
   Model = glm::rotate(Model, rotZ, glm::vec3(0, 0, 1));

   res->mvp = res->clip * res->projection * res->view * Model;
}

static void
scene_update(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;

   // MVP in UBO
   update_mvp(res);
   vkdf_buffer_map_and_fill(ctx, res->ubo, 0, sizeof(res->mvp), &res->mvp);
}

static void
check_data_32bits(DemoResources *res,
                  float *image_feedback,
                  float *ssbo_feedback,
                  unsigned &count_different,
                  unsigned &count_painted)
{
   unsigned index_image = 0;
   unsigned index_ssbo = 0;

   count_different = 0;
   count_painted = 0;
   for (unsigned pixel = 0; pixel < res->num_pixels; pixel++) {
      glm::vec4 image_color;
      float ssbo_color;

      for (unsigned component = 0; component < res->num_image_components;
           component ++) {
         image_color[component] = image_feedback[index_image];
         index_image++;
      }

      ssbo_color = ssbo_feedback[index_ssbo];
      index_ssbo++;

      if (ssbo_color != image_color.r) {
         count_different++;
      }

      if (image_color.r != REFERENCE_VALUE) {
         count_painted++;
      }
   }

   assert(index_image == res->num_image_elements);
   assert(index_ssbo == res->num_ssbo_elements);
}

static void
check_data_16bits(DemoResources *res,
                  ushort *image_feedback,
                  ushort *ssbo_feedback,
                  unsigned &count_different,
                  unsigned &count_painted)
{
   unsigned index_image = 0;
   unsigned index_ssbo = 0;
   count_different = 0;
   count_painted = 0;

   for (unsigned pixel = 0; pixel < res->num_pixels; pixel++) {
      ushort image_color[4];
      ushort ssbo_color;

      for (unsigned component = 0; component < res->num_image_components;
           component ++) {
         image_color[component] = image_feedback[index_image];
         index_image++;
      }

      ssbo_color = ssbo_feedback[index_ssbo];
      index_ssbo++;

      if (ssbo_color != image_color[0]) {
         count_different++;
      }

      if (image_color[0] != USHORT_REFERENCE_VALUE) {
         count_painted++;
      }
   }

   assert(index_image == res->num_image_elements);
   assert(index_ssbo == res->num_ssbo_elements);
}

//fetch ssbo, fetch what was drawn (expected), compare, print outcome
static void
check_outcome(VkdfContext *ctx, DemoResources *res)
{
   bool result = true;
   unsigned count_different = 0;
   unsigned count_painted = 0;

   vkdf_buffer_map_and_get(ctx, res->ssbo, 0,
                           res->ssbo_size, res->ssbo_feedback);

   vkdf_image_map_and_get(ctx, res->color_image, 0,
                          res->image_size, res->image_feedback);

   if (res->bits == 32)
      check_data_32bits(res, (float*) res->image_feedback, (float*) res->ssbo_feedback,
                        count_different, count_painted);
   else
      check_data_16bits(res, (ushort*) res->image_feedback, (ushort*) res->ssbo_feedback,
                        count_different, count_painted);

   result = count_different == 0;

   fprintf(stdout, "%i pixels out of %i are painted (different to reference value), %2.2f%%\n",
           count_painted, res->num_pixels, 100.0f * count_painted / res->num_pixels);

   fprintf(stdout, "%i pixels have a difference between rendered and stored on ssbo (%2.2f%%)\n",
           count_different,
           100.0f * count_different / res->num_pixels);

   if (result)
      fprintf(stdout, "Correct.\n");
   else
      fprintf(stdout, "WRONG: ssbo != image \n");
}

static void
scene_render(VkdfContext *ctx, void *data)
{
   DemoResources *res = (DemoResources *) data;
   //FIXME: hack to test only first time. Would not be needed if we do a one-draw
   static unsigned int count = 0;

   // Offscreen rendering
   VkPipelineStageFlags pipeline_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_execute(ctx,
                               res->offscreen_cmd_buf,
                               &pipeline_stage,
                               0, NULL,
                               1, &res->offscreen_draw_sem);

   if (count == 0)
      check_outcome(ctx, res);

   //NOTE: for the purpose of this demos, we don't need to present the
   //      content. Useful for debugging.

   // Technically we don't need to wait for the offscreen rendering to
   // finish. Keep it in any case.
   VkSemaphore copy_wait_sems[2] = {
      ctx->acquired_sem[ctx->swap_chain_index],
      res->offscreen_draw_sem
   };
   VkPipelineStageFlags pipeline_stages_present[2] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
   };
   vkdf_command_buffer_execute(ctx,
                               res->onscreen_cmd_bufs[ctx->swap_chain_index],
                               pipeline_stages_present,
                               2, copy_wait_sems,
                               1, &ctx->draw_sem[ctx->swap_chain_index]);

   count++;
}

static void
destroy_pipeline_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyPipeline(ctx->device, res->pipeline_onscreen, NULL);
   vkDestroyPipeline(ctx->device, res->pipeline_offscreen, NULL);
   vkDestroyPipelineLayout(ctx->device, res->pipeline_layout, NULL);
}

static void
destroy_framebuffer_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyFramebuffer(ctx->device, res->framebuffer, NULL);
}

static void
destroy_shader_resources(VkdfContext *ctx, DemoResources *res)
{
  vkDestroyShaderModule(ctx->device, res->vs_module, NULL);
  vkDestroyShaderModule(ctx->device, res->fs_module, NULL);
}

static void
destroy_command_buffer_resources(VkdfContext *ctx, DemoResources *res)
{
   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        ctx->swap_chain_length,
                        res->onscreen_cmd_bufs);
   vkFreeCommandBuffers(ctx->device,
                        res->cmd_pool,
                        1,
                        &res->offscreen_cmd_buf);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
}

static void
destroy_descriptor_resources(VkdfContext *ctx, DemoResources *res)
{
   vkFreeDescriptorSets(ctx->device,
                        res->descriptor_pool_ubo, 1, &res->descriptor_set_ubo);
   vkFreeDescriptorSets(ctx->device,
                        res->descriptor_pool_ssbo, 1, &res->descriptor_set_ssbo);

   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout_ubo, NULL);
   vkDestroyDescriptorSetLayout(ctx->device, res->set_layout_ssbo, NULL);

   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool_ubo, NULL);
   vkDestroyDescriptorPool(ctx->device, res->descriptor_pool_ssbo, NULL);
}

static void
destroy_ubo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->ubo.buf, NULL);
   vkFreeMemory(ctx->device, res->ubo.mem, NULL);
}

static void
destroy_ssbo_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroyBuffer(ctx->device, res->ssbo.buf, NULL);
   vkFreeMemory(ctx->device, res->ssbo.mem, NULL);
}

static void
destroy_feedback_data(VkdfContext *ctx, DemoResources *res)
{
   free(res->ssbo_feedback);
   free(res->image_feedback);
}

static void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   vkDestroySemaphore(ctx->device, res->offscreen_draw_sem, NULL);
   destroy_pipeline_resources(ctx, res);
   vkDestroyRenderPass(ctx->device, res->onscreen_render_pass, NULL);
   vkDestroyRenderPass(ctx->device, res->offscreen_render_pass, NULL);
   vkdf_destroy_buffer(ctx, &res->vertex_buf);
   destroy_descriptor_resources(ctx, res);
   destroy_ubo_resources(ctx, res);
   destroy_ssbo_resources(ctx, res);
   destroy_framebuffer_resources(ctx, res);
   destroy_shader_resources(ctx, res);
   destroy_command_buffer_resources(ctx, res);
   vkdf_destroy_image(ctx, &res->color_image);
   destroy_feedback_data(ctx, res);
}

static int
print_usage_and_exit()
{
   fprintf(stdout, "Usage: ./ssbo [num_components] [bits]\n");
   fprintf(stdout, "\tbits needs to be 16 or 32\n");

   return 1;
}

int
main(int argc, char *argv[])
{
   VkdfContext ctx;
   DemoResources resources;
   unsigned bits = DEFAULT_BITS;

   if (argc > 2)
      return print_usage_and_exit();

   if (argc > 1) {
      bits = atoi(argv[1]);
      if (bits != 16 && bits != 32)
         return print_usage_and_exit();
   }

   vkdf_init(&ctx, WIDTH, HEIGHT, false, false, ENABLE_DEBUG);
   init_resources(&ctx, &resources, bits);

   vkdf_event_loop_run(&ctx, scene_update, scene_render, &resources);

   // FIXME: final version doesn't need a loop, just below stuff. For now
   // using the loop to check that we are seeing the triangle

   // scene_update(&ctx, &resources);
   // scene_render(&ctx, &resources);

   cleanup_resources(&ctx, &resources);
   vkdf_cleanup(&ctx);

   return 0;
}
