#include "vkdf.hpp"

const uint8_t WIDTH = 128;
const uint8_t HEIGHT = 128;

typedef struct {
   VkCommandPool cmd_pool;
   VkCommandBuffer cmd_buf;
   VkRenderPass render_pass;
   VkFramebuffer framebuffer;
   VkdfImage color_image;
   VkdfBuffer color_buffer;
} DemoResources;

static void
prepare_image_for_transfer(VkCommandBuffer cmd_buf, VkImage image)
{
   VkImageSubresourceRange range =
      vkdf_create_image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
   VkImageMemoryBarrier barrier =
      vkdf_create_image_barrier(VK_ACCESS_SHADER_WRITE_BIT,
                                VK_ACCESS_TRANSFER_READ_BIT,
                                VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_GENERAL,
                                image, range);
   vkCmdPipelineBarrier(cmd_buf,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0,
                        0, NULL, 0, NULL, 1, &barrier);
}

static void
prepare_buffer_for_host_read(VkCommandBuffer cmd_buf,
                             VkBuffer buffer,
                             VkDeviceSize offset,
                             VkDeviceSize size)
{
   VkBufferMemoryBarrier barrier =
      vkdf_create_buffer_barrier(VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_HOST_READ_BIT,
                                 buffer, offset, size);
   vkCmdPipelineBarrier(cmd_buf,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT,
                        0,
                        0, NULL, 1, &barrier, 0, NULL);
}

static void
record_command_buffer(VkdfContext *ctx, DemoResources *res)
{
   vkdf_create_command_buffer(ctx,
                              res->cmd_pool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1, &res->cmd_buf);

   vkdf_command_buffer_begin(res->cmd_buf,
                             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

   VkClearValue clear_values[1];
   vkdf_color_clear_set(&clear_values[0], glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));

   VkRenderPassBeginInfo rp_begin;
   rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
   rp_begin.pNext = NULL;
   rp_begin.renderPass = res->render_pass;
   rp_begin.framebuffer = res->framebuffer;
   rp_begin.renderArea.offset.x = 0;
   rp_begin.renderArea.offset.y = 0;
   rp_begin.renderArea.extent.width = ctx->width;
   rp_begin.renderArea.extent.height = ctx->height;
   rp_begin.clearValueCount = 1;
   rp_begin.pClearValues = clear_values;

   vkCmdBeginRenderPass(res->cmd_buf,
                        &rp_begin,
                        VK_SUBPASS_CONTENTS_INLINE);

   vkCmdEndRenderPass(res->cmd_buf);

   prepare_image_for_transfer(res->cmd_buf, res->color_image.image);

   VkBufferImageCopy region;
   region.bufferOffset = 0;
   region.bufferRowLength = 0;
   region.bufferImageHeight = 0;
   region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
   region.imageOffset = { 0, 0, 0 };
   region.imageExtent = { WIDTH, HEIGHT, 1 };

   vkCmdCopyImageToBuffer(res->cmd_buf,
                          res->color_image.image, VK_IMAGE_LAYOUT_GENERAL,
                          res->color_buffer.buf, 1, &region);

   prepare_buffer_for_host_read(res->cmd_buf,
                                res->color_buffer.buf, 0, VK_WHOLE_SIZE);

   vkdf_command_buffer_end(res->cmd_buf);
}

static void
init_resources(VkdfContext *ctx, DemoResources *res)
{
   memset(res, 0, sizeof(DemoResources));

   res->color_image =
      vkdf_create_image(ctx,
                        ctx->width,
                        ctx->height,
                        1,
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_VIEW_TYPE_2D);

#if 0
   /* Initializes the color image pixel data */
   const uint32_t image_bytes = WIDTH * HEIGHT * 4 * sizeof(uint8_t);

   uint8_t *data;
   vkdf_memory_map(ctx, res->color_image.mem, 0, image_bytes, (void **)&data);

   VkMappedMemoryRange range;
   range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.pNext = NULL;
   range.memory = res->color_image.mem;
   range.offset = 0;
   range.size = image_bytes;
   vkInvalidateMappedMemoryRanges(ctx->device, 1, &range);

   for (int i = 0; i < WIDTH; i++) {
      for (int j = 0; j < HEIGHT; j++) {
          data[(i * WIDTH + j) * 4 + 0] = 0;
          data[(i * WIDTH + j) * 4 + 1] = 0;
          data[(i * WIDTH + j) * 4 + 2] = 255;
          data[(i * WIDTH + j) * 4 + 3] = 255;
      }
   }

   vkdf_memory_unmap(ctx, res->color_image.mem, res->color_buffer.mem_props,
                     0, image_bytes);
#endif

   res->color_buffer =
        vkdf_create_buffer(ctx, 0,
                           4 * ctx->width * ctx->height * sizeof(float),
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

   res->render_pass =
      vkdf_renderpass_simple_new(ctx,
                                 VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 VK_ATTACHMENT_STORE_OP_STORE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 VK_FORMAT_UNDEFINED,
                                 VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_UNDEFINED);

   res->framebuffer =
      vkdf_create_framebuffer(ctx,
                              res->render_pass,
                              res->color_image.view,
                              ctx->width,
                              ctx->height,
                              0, NULL);

   res->cmd_pool = vkdf_create_gfx_command_pool(ctx, 0);
   record_command_buffer(ctx, res);
}

static void
write_pixels_to_file(VkdfContext *ctx, DemoResources *res)
{
   const uint32_t image_bytes = WIDTH * HEIGHT * 4 * sizeof(uint8_t);

   uint8_t *data;
   vkdf_memory_map(ctx, res->color_buffer.mem, 0, image_bytes, (void **)&data);

   VkMappedMemoryRange range;
   range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
   range.pNext = NULL;
   range.memory = res->color_buffer.mem;
   range.offset = 0;
   range.size = image_bytes;
   vkInvalidateMappedMemoryRanges(ctx->device, 1, &range);

   FILE *out = fopen("out.tga","wb");
   uint8_t TGAhead[] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         WIDTH, 0, HEIGHT, 0, 24, 0 };
   fwrite(&TGAhead, sizeof(TGAhead), 1, out);
   for (int i = 0; i < WIDTH; i++) {
      for (int j = 0; j < HEIGHT; j++) {
          fwrite(&data[(i * WIDTH + j) * 4] + 2, 1, 1, out); // B
          fwrite(&data[(i * WIDTH + j) * 4] + 1, 1, 1, out); // G
          fwrite(&data[(i * WIDTH + j) * 4] + 0, 1, 1, out); // R
      }
   }
   fclose(out);

   vkdf_memory_unmap(ctx, res->color_buffer.mem, res->color_buffer.mem_props,
                     0, image_bytes);
}

static void
scene_render(VkdfContext *ctx, DemoResources *res)
{
   VkPipelineStageFlags pipeline_stages =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

   vkdf_command_buffer_execute(ctx,
                               res->cmd_buf,
                               &pipeline_stages,
                               0, NULL, 0, NULL);

   vkDeviceWaitIdle(ctx->device);

   write_pixels_to_file(ctx, res);
}

void
cleanup_resources(VkdfContext *ctx, DemoResources *res)
{
   vkdf_destroy_buffer(ctx, &res->color_buffer);
   vkdf_destroy_image(ctx, &res->color_image);

   vkDestroyRenderPass(ctx->device, res->render_pass, NULL);

   vkDestroyFramebuffer(ctx->device, res->framebuffer, NULL);

   vkFreeCommandBuffers(ctx->device, res->cmd_pool, 1, &res->cmd_buf);
   vkDestroyCommandPool(ctx->device, res->cmd_pool, NULL);
}

int
main()
{
   VkdfContext ctx;
   DemoResources resources;

   vkdf_init(&ctx, WIDTH, HEIGHT, false, false, false);
   init_resources(&ctx, &resources);

   scene_render(&ctx, &resources);

   cleanup_resources(&ctx, &resources);

   vkdf_cleanup(&ctx);

   return 0;
}