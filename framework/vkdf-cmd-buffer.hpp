#ifndef __VKDF_CMD_BUFFER_H__
#define __VKDF_CMD_BUFFER_H__

VkCommandPool
vkdf_create_gfx_command_pool(VkdfContext *ctx,
                             VkCommandPoolCreateFlags flags);

void
vkdf_create_command_buffer(VkdfContext *ctx,
                           VkCommandPool cmd_pool,
                           VkCommandBufferLevel level,
                           uint32_t cmd_count,
                           VkCommandBuffer *cmd_bufs);

void
vkdf_command_buffer_begin(VkCommandBuffer cmd_buf,
                          VkCommandBufferUsageFlags flags);

void
vkdf_command_buffer_end(VkCommandBuffer cmd_buf);

void
vkdf_command_buffer_execute(VkdfContext *ctx,
                            VkCommandBuffer cmd_buf,
                            VkPipelineStageFlags *pipeline_stage_flags,
                            uint32_t wait_sem_count,
                            VkSemaphore *wait_sem,
                            uint32_t signal_sem_count,
                            VkSemaphore *signal_sem);

void
vkdf_command_buffer_execute_sync(VkdfContext *ctx,
                                 VkCommandBuffer cmd_buf,
                                 VkPipelineStageFlags pipeline_stage_flags);

#endif
