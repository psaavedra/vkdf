#ifndef __VKDF_DESCRIPTOR_H__
#define __VKDF_DESCRIPTOR_H__

VkDescriptorPool
vkdf_create_descriptor_pool(VkdfContext *ctx,
                            VkDescriptorType type,
                            uint32_t count);

VkDescriptorSetLayout
vkdf_create_ubo_descriptor_set_layout(VkdfContext *ctx,
                                      uint32_t binding,
                                      uint32_t count,
                                      VkShaderStageFlags stages,
                                      bool is_dynamic);

VkDescriptorSetLayout
vkdf_create_sampler_descriptor_set_layout(VkdfContext *ctx,
                                          uint32_t binding,
                                          uint32_t count,
                                          VkShaderStageFlags stages);

void
vkdf_descriptor_set_buffer_update(VkdfContext *ctx,
                                  VkDescriptorSet descriptor,
                                  VkBuffer buffer,
                                  uint32_t binding,
                                  uint32_t count,
                                  VkDeviceSize *offsets,
                                  VkDeviceSize *ranges,
                                  bool is_dynamic);

void
vkdf_descriptor_set_sampler_update(VkdfContext *ctx,
                                   VkDescriptorSet descriptor,
                                   VkSampler sampler,
                                   VkImageView view,
                                   VkImageLayout layout);

#endif
