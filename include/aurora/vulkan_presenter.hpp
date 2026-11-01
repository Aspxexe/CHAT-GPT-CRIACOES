#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aurora {
class VulkanPresenter {
public:
    explicit VulkanPresenter(void* hwnd);
    ~VulkanPresenter();
    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;

    void present(const std::vector<std::uint32_t>& rgba, std::uint32_t width,
                 std::uint32_t height);
    std::string deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
