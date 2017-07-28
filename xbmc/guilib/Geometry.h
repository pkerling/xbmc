/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#ifdef __GNUC__
// under gcc, inline will only take place if optimizations are applied (-O). this will force inline even with optimizations.
#define XBMC_FORCE_INLINE __attribute__((always_inline))
#else
#define XBMC_FORCE_INLINE
#endif

#include <algorithm>
#include <stdexcept>
#include <vector>

template <typename T> class CPointGen
{
public:
  typedef CPointGen<T> this_type;

  CPointGen<T>()
  {
    x = 0; y = 0;
  };

  CPointGen<T>(T a, T b)
  {
    x = a;
    y = b;
  };

  template <class U> explicit CPointGen<T>(const CPointGen<U>& rhs)
  {
    x = rhs.x;
    y = rhs.y;
  }

  this_type operator+(const this_type &point) const
  {
    this_type ans;
    ans.x = x + point.x;
    ans.y = y + point.y;
    return ans;
  };

  const this_type &operator+=(const this_type &point)
  {
    x += point.x;
    y += point.y;
    return *this;
  };

  this_type operator-(const this_type &point) const
  {
    CPointGen<T> ans;
    ans.x = x - point.x;
    ans.y = y - point.y;
    return ans;
  };

  const this_type &operator-=(const this_type &point)
  {
    x -= point.x;
    y -= point.y;
    return *this;
  };

  bool operator !=(const this_type &point) const
  {
    if (x != point.x) return true;
    if (y != point.y) return true;
    return false;
  };

  T x, y;
};

/**
 * Generic two-dimensional size representation
 *
 * Class invariant: width and height are both non-negative
 * Throws std::out_of_range if invariant would be violated. The class
 * is exception-safe. If modification would violate the invariant, the size
 * is not changed.
 */
template <typename T> class CSizeGen
{
  T m_w, m_h;

  void CheckSet(T width, T height)
  {
    if (width < 0)
    {
      throw std::out_of_range("Size may not have negative width");
    }
    if (height < 0)
    {
      throw std::out_of_range("Size may not have negative height");
    }
    m_w = width;
    m_h = height;
  }

public:
  typedef CSizeGen<T> this_type;

  T Width() const
  {
    return m_w;
  }

  T Height() const
  {
    return m_h;
  }

  void SetWidth(T width)
  {
    CheckSet(width, m_h);
  }

  void SetHeight(T height)
  {
    CheckSet(m_w, height);
  }

  void Set(T width, T height)
  {
    CheckSet(width, height);
  }

  CSizeGen()
  : m_w{}, m_h{}
  {}

  CSizeGen(T width, T height)
  {
    CheckSet(width, height);
  }

  bool IsZero() const
  {
    return (m_w == static_cast<T> (0) && m_h == static_cast<T> (0));
  }

  unsigned int Area() const
  {
    return m_w * m_h;
  }

  CPointGen<T> ToPoint() const
  {
    return {m_w, m_h};
  }

  template <class U> CSizeGen<T>(const CSizeGen<U>& rhs)
  {
    CheckSet(rhs.m_w, rhs.m_h);
  }

  this_type operator+(const this_type& size) const
  {
    return {m_w + size.m_w, m_h + size.m_h};
  };

  this_type& operator+=(const this_type& size)
  {
    CheckSet(m_w + size.m_w, m_h + size.m_h);
    return *this;
  };

  this_type operator-(const this_type& size) const
  {
    return {m_w - size.m_w, m_h - size.m_h};
  };

  this_type& operator-=(const this_type& size)
  {
    CheckSet(m_w - size.m_w, m_h - size.m_h);
    return *this;
  };

  this_type operator*(T factor) const
  {
    return {m_w * factor, m_h * factor};
  }

  this_type& operator*=(T factor)
  {
    CheckSet(m_w * factor, m_h * factor);
    return *this;
  }

  this_type operator/(T factor) const
  {
    return {m_w / factor, m_h / factor};
  }

  CSizeGen<T>& operator/=(T factor)
  {
    CheckSet(m_w / factor, m_h / factor);
    return *this;
  }

  bool operator==(const this_type& size) const
  {
    return (m_w == size.m_w && m_h == size.m_h);
  }

  bool operator!=(const this_type& size) const
  {
    return !(*this == size);
  }
};

template <typename T> class CRectGen
{
public:
  typedef CRectGen<T> this_type;

  CRectGen<T>() { x1 = y1 = x2 = y2 = 0;};
  CRectGen<T>(T left, T top, T right, T bottom) { x1 = left; y1 = top; x2 = right; y2 = bottom; };
  CRectGen<T>(const CPointGen<T> &p1, const CPointGen<T> &p2)
  {
    x1 = p1.x;
    y1 = p1.y;
    x2 = p2.x;
    y2 = p2.y;
  }
  CRectGen<T>(const CPointGen<T> &origin, const CSizeGen<T> &size)
  {
    x1 = origin.x;
    y1 = origin.y;
    x2 = x1 + size.Width();
    y2 = y1 + size.Height();
  }

  template <class U> explicit CRectGen<T>(const CRectGen<U>& rhs)
  {
    x1 = rhs.x1;
    y1 = rhs.y1;
    x2 = rhs.x2;
    y2 = rhs.y2;
  }

  void SetRect(T left, T top, T right, T bottom) { x1 = left; y1 = top; x2 = right; y2 = bottom; };

  bool PtInRect(const CPointGen<T> &point) const
  {
    if (x1 <= point.x && point.x <= x2 && y1 <= point.y && point.y <= y2)
      return true;
    return false;
  };

  inline const this_type &operator -=(const CPointGen<T> &point) XBMC_FORCE_INLINE
  {
    x1 -= point.x;
    y1 -= point.y;
    x2 -= point.x;
    y2 -= point.y;
    return *this;
  };

  inline const this_type &operator +=(const CPointGen<T> &point) XBMC_FORCE_INLINE
  {
    x1 += point.x;
    y1 += point.y;
    x2 += point.x;
    y2 += point.y;
    return *this;
  };

  const this_type &Intersect(const this_type &rect)
  {
    x1 = clamp_range(x1, rect.x1, rect.x2);
    x2 = clamp_range(x2, rect.x1, rect.x2);
    y1 = clamp_range(y1, rect.y1, rect.y2);
    y2 = clamp_range(y2, rect.y1, rect.y2);
    return *this;
  };

  const this_type &Union(const this_type &rect)
  {
    if (IsEmpty())
      *this = rect;
    else if (!rect.IsEmpty())
    {
      x1 = std::min(x1,rect.x1);
      y1 = std::min(y1,rect.y1);

      x2 = std::max(x2,rect.x2);
      y2 = std::max(y2,rect.y2);
    }

    return *this;
  };

  inline bool IsEmpty() const XBMC_FORCE_INLINE
  {
    return (x2 - x1) * (y2 - y1) == 0;
  };

  inline CPointGen<T> P1() const XBMC_FORCE_INLINE
  {
    return CPointGen<T>(x1, y1);
  }

  inline CPointGen<T> P2() const XBMC_FORCE_INLINE
  {
    return CPointGen<T>(x2, y2);
  }

  inline T Width() const XBMC_FORCE_INLINE
  {
    return x2 - x1;
  };

  inline T Height() const XBMC_FORCE_INLINE
  {
    return y2 - y1;
  };

  inline T Area() const XBMC_FORCE_INLINE
  {
    return Width() * Height();
  };

  CSizeGen<T> ToSize() const
  {
    return {Width(), Height()};
  };

  std::vector<this_type> SubtractRect(this_type splitterRect)
  {
    std::vector<this_type> newRectanglesList;
    this_type intersection = splitterRect.Intersect(*this);

    if (!intersection.IsEmpty())
    {
      this_type add;

      // add rect above intersection if not empty
      add = this_type(x1, y1, x2, intersection.y1);
      if (!add.IsEmpty())
        newRectanglesList.push_back(add);

      // add rect below intersection if not empty
      add = this_type(x1, intersection.y2, x2, y2);
      if (!add.IsEmpty())
        newRectanglesList.push_back(add);

      // add rect left intersection if not empty
      add = this_type(x1, intersection.y1, intersection.x1, intersection.y2);
      if (!add.IsEmpty())
        newRectanglesList.push_back(add);

      // add rect right intersection if not empty
      add = this_type(intersection.x2, intersection.y1, x2, intersection.y2);
      if (!add.IsEmpty())
        newRectanglesList.push_back(add);
    }
    else
    {
      newRectanglesList.push_back(*this);
    }

    return newRectanglesList;
  }

  std::vector<this_type> SubtractRects(std::vector<this_type > intersectionList)
  {
    std::vector<this_type> fragmentsList;
    fragmentsList.push_back(*this);

    for (typename std::vector<this_type>::iterator splitter = intersectionList.begin(); splitter != intersectionList.end(); ++splitter)
    {
      typename std::vector<this_type> toAddList;

      for (typename std::vector<this_type>::iterator fragment = fragmentsList.begin(); fragment != fragmentsList.end(); ++fragment)
      {
        std::vector<this_type> newFragmentsList = fragment->SubtractRect(*splitter);
        toAddList.insert(toAddList.end(), newFragmentsList.begin(), newFragmentsList.end());
      }

      fragmentsList.clear();
      fragmentsList.insert(fragmentsList.end(), toAddList.begin(), toAddList.end());
    }

    return fragmentsList;
  }

  bool operator !=(const this_type &rect) const
  {
    if (x1 != rect.x1) return true;
    if (x2 != rect.x2) return true;
    if (y1 != rect.y1) return true;
    if (y2 != rect.y2) return true;
    return false;
  };

  T x1, y1, x2, y2;
private:
  inline static T clamp_range(T x, T l, T h) XBMC_FORCE_INLINE
  {
    return (x > h) ? h : ((x < l) ? l : x);
  }
};

typedef CPointGen<float> CPoint;
typedef CPointGen<int>   CPointInt;

typedef CSizeGen<float> CSize;
typedef CSizeGen<int> CSizeInt;

typedef CRectGen<float>  CRect;
typedef CRectGen<int>    CRectInt;
