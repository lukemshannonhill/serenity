/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/MappedFile.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/PNGLoader.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

namespace Gfx {

NonnullRefPtr<Bitmap> Bitmap::create(Format format, const Size& size)
{
    return adopt(*new Bitmap(format, size, Purgeable::No));
}

NonnullRefPtr<Bitmap> Bitmap::create_purgeable(Format format, const Size& size)
{
    return adopt(*new Bitmap(format, size, Purgeable::Yes));
}

Bitmap::Bitmap(Format format, const Size& size, Purgeable purgeable)
    : m_size(size)
    , m_pitch(round_up_to_power_of_two(size.width() * sizeof(RGBA32), 16))
    , m_format(format)
    , m_purgeable(purgeable == Purgeable::Yes)
{
    ASSERT(!m_size.is_empty());
    if (format == Format::Indexed8)
        m_palette = new RGBA32[256];
    int map_flags = purgeable == Purgeable::Yes ? (MAP_PURGEABLE | MAP_PRIVATE) : (MAP_ANONYMOUS | MAP_PRIVATE);
    m_data = (RGBA32*)mmap_with_name(nullptr, size_in_bytes(), PROT_READ | PROT_WRITE, map_flags, 0, 0, String::format("GraphicsBitmap [%dx%d]", width(), height()).characters());
    ASSERT(m_data && m_data != (void*)-1);
    m_needs_munmap = true;
}

NonnullRefPtr<Bitmap> Bitmap::create_wrapper(Format format, const Size& size, size_t pitch, RGBA32* data)
{
    return adopt(*new Bitmap(format, size, pitch, data));
}

RefPtr<Bitmap> Bitmap::load_from_file(const StringView& path)
{
    return load_png(path);
}

RefPtr<Bitmap> Bitmap::load_from_file(Format format, const StringView& path, const Size& size)
{
    MappedFile mapped_file(path);
    if (!mapped_file.is_valid())
        return nullptr;
    return adopt(*new Bitmap(format, size, move(mapped_file)));
}

Bitmap::Bitmap(Format format, const Size& size, size_t pitch, RGBA32* data)
    : m_size(size)
    , m_data(data)
    , m_pitch(pitch)
    , m_format(format)
{
    if (format == Format::Indexed8)
        m_palette = new RGBA32[256];
}

Bitmap::Bitmap(Format format, const Size& size, MappedFile&& mapped_file)
    : m_size(size)
    , m_data((RGBA32*)mapped_file.data())
    , m_pitch(round_up_to_power_of_two(size.width() * sizeof(RGBA32), 16))
    , m_format(format)
    , m_mapped_file(move(mapped_file))
{
    ASSERT(format != Format::Indexed8);
}

NonnullRefPtr<Bitmap> Bitmap::create_with_shared_buffer(Format format, NonnullRefPtr<SharedBuffer>&& shared_buffer, const Size& size)
{
    return adopt(*new Bitmap(format, move(shared_buffer), size));
}

Bitmap::Bitmap(Format format, NonnullRefPtr<SharedBuffer>&& shared_buffer, const Size& size)
    : m_size(size)
    , m_data((RGBA32*)shared_buffer->data())
    , m_pitch(round_up_to_power_of_two(size.width() * sizeof(RGBA32), 16))
    , m_format(format)
    , m_shared_buffer(move(shared_buffer))
{
    ASSERT(format != Format::Indexed8);
}

NonnullRefPtr<Bitmap> Bitmap::to_shareable_bitmap() const
{
    if (m_shared_buffer)
        return *this;
    auto buffer = SharedBuffer::create_with_size(size_in_bytes());
    auto bitmap = Bitmap::create_with_shared_buffer(m_format, *buffer, m_size);
    memcpy(buffer->data(), scanline(0), size_in_bytes());
    return bitmap;
}

Bitmap::~Bitmap()
{
    if (m_needs_munmap) {
        int rc = munmap(m_data, size_in_bytes());
        ASSERT(rc == 0);
    }
    m_data = nullptr;
    delete[] m_palette;
}

void Bitmap::set_mmap_name(const StringView& name)
{
    ASSERT(m_needs_munmap);
    ::set_mmap_name(m_data, size_in_bytes(), String(name).characters());
}

void Bitmap::fill(Color color)
{
    ASSERT(m_format == Bitmap::Format::RGB32 || m_format == Bitmap::Format::RGBA32);
    for (int y = 0; y < height(); ++y) {
        auto* scanline = this->scanline(y);
        fast_u32_fill(scanline, color.value(), width());
    }
}

void Bitmap::set_volatile()
{
    ASSERT(m_purgeable);
    if (m_volatile)
        return;
    int rc = madvise(m_data, size_in_bytes(), MADV_SET_VOLATILE);
    if (rc < 0) {
        perror("madvise(MADV_SET_VOLATILE)");
        ASSERT_NOT_REACHED();
    }
    m_volatile = true;
}

[[nodiscard]] bool Bitmap::set_nonvolatile()
{
    ASSERT(m_purgeable);
    if (!m_volatile)
        return true;
    int rc = madvise(m_data, size_in_bytes(), MADV_SET_NONVOLATILE);
    if (rc < 0) {
        perror("madvise(MADV_SET_NONVOLATILE)");
        ASSERT_NOT_REACHED();
    }
    m_volatile = false;
    return rc == 0;
}

}
