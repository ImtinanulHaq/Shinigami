/**
 * @file    proxy_shared_memory.cpp
 * @brief   ProxyShmRegion implementation.
 */

#include "proxy_shared_memory.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <utility>   // std::exchange

namespace middleware {

ProxyShmRegion::ProxyShmRegion()
    : ptr_(nullptr), size_(0), shm_fd_(-1), owner_(false)
{}

ProxyShmRegion::~ProxyShmRegion() {
    close(owner_);
}

ProxyShmRegion::ProxyShmRegion(ProxyShmRegion&& o) noexcept
    : ptr_(std::exchange(o.ptr_,    nullptr))
    , size_(std::exchange(o.size_,  0))
    , shm_fd_(std::exchange(o.shm_fd_, -1))
    , name_(std::move(o.name_))
    , owner_(std::exchange(o.owner_, false))
{}

ProxyShmRegion& ProxyShmRegion::operator=(ProxyShmRegion&& o) noexcept {
    if (this != &o) {
        close(owner_);
        ptr_    = std::exchange(o.ptr_,    nullptr);
        size_   = std::exchange(o.size_,   0);
        shm_fd_ = std::exchange(o.shm_fd_, -1);
        name_   = std::move(o.name_);
        owner_  = std::exchange(o.owner_,  false);
    }
    return *this;
}

// ── create ────────────────────────────────────────────────────────────────

Result<void> ProxyShmRegion::create(std::string_view name, size_t size_bytes) {
    if (ptr_) {
        return Result<void>::err(ProxyError::AlreadyConnected,
                                 "SHM region already open");
    }

    std::string n(name);
    int fd = ::shm_open(n.c_str(), O_CREAT | O_EXCL | O_RDWR,
                        S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return Result<void>::err(ProxyError::ShmCreateFailed,
                                 ::strerror(errno));
    }

    if (::ftruncate(fd, static_cast<off_t>(size_bytes)) < 0) {
        ::shm_unlink(n.c_str());
        ::close(fd);
        return Result<void>::err(ProxyError::ShmCreateFailed,
                                 "ftruncate failed");
    }

    void* p = ::mmap(nullptr, size_bytes,
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ::shm_unlink(n.c_str());
        ::close(fd);
        return Result<void>::err(ProxyError::ShmCreateFailed,
                                 "mmap failed on create");
    }

    ptr_    = p;
    size_   = size_bytes;
    shm_fd_ = fd;
    name_   = std::move(n);
    owner_  = true;
    return Result<void>::ok();
}

// ── open ──────────────────────────────────────────────────────────────────

Result<void> ProxyShmRegion::open(std::string_view name, size_t min_size) {
    if (ptr_) {
        return Result<void>::err(ProxyError::AlreadyConnected,
                                 "SHM region already open");
    }

    std::string n(name);
    int fd = ::shm_open(n.c_str(), O_RDWR, 0);
    if (fd < 0) {
        return Result<void>::err(ProxyError::ShmCreateFailed,
                                 ::strerror(errno));
    }

    // Determine actual size.
    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        return Result<void>::err(ProxyError::ShmCreateFailed,
                                 "fstat on SHM failed");
    }

    if (static_cast<size_t>(st.st_size) < min_size) {
        ::close(fd);
        return Result<void>::err(ProxyError::ShmSizeMismatch,
                                 "SHM smaller than expected — version skew?");
    }

    size_t actual = static_cast<size_t>(st.st_size);
    void*  p      = ::mmap(nullptr, actual,
                           PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        ::close(fd);
        return Result<void>::err(ProxyError::ShmCreateFailed,
                                 "mmap failed on open");
    }

    ptr_    = p;
    size_   = actual;
    shm_fd_ = fd;
    name_   = std::move(n);
    owner_  = false;
    return Result<void>::ok();
}

// ── close ─────────────────────────────────────────────────────────────────

void ProxyShmRegion::close(bool unlink) noexcept {
    if (ptr_) {
        ::munmap(ptr_, size_);
        ptr_  = nullptr;
        size_ = 0;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
    if (unlink && !name_.empty()) {
        ::shm_unlink(name_.c_str());
    }
    name_.clear();
    owner_ = false;
}

} // namespace middleware
