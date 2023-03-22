/*
 *	PROGRAM:	JRD Access Method
 *	MODULE:		auto.h
 *	DESCRIPTION:	STL::auto_ptr replacement
 *
 *		*** warning ***
 *  Unlike STL's auto_ptr ALWAYS deletes ptr in destructor -
 *  no ownership flag!
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Alexander Peshkoff
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Alexander Peshkoff <peshkoff@mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef CLASSES_AUTO_PTR_H
#define CLASSES_AUTO_PTR_H

#include <stdio.h>

namespace Firebird {


template <typename What>
class SimpleDelete
{
public:
	static void clear(What* ptr)
	{
		delete ptr;
	}
};

template <>
inline void SimpleDelete<FILE>::clear(FILE* f)
{
	if (f) {
		fclose(f);
	}
}


template <typename What>
class ArrayDelete
{
public:
	static void clear(What* ptr)
	{
		delete[] ptr;
	}
};


template <typename T>
class SimpleRelease
{
public:
	static void clear(T* ptr)
	{
		if (ptr)
		{
			ptr->release();
		}
	}
};


template <typename T>
class SimpleDispose
{
public:
	static void clear(T* ptr)
	{
		if (ptr)
		{
			ptr->dispose();
		}
	}
};


template <typename Where, template <typename W> class Clear = SimpleDelete >
class AutoPtr
{
private:
	Where* ptr;
public:
	AutoPtr(Where* v = NULL)
		: ptr(v)
	{}

	~AutoPtr()
	{
		Clear<Where>::clear(ptr);
	}

	AutoPtr& operator= (Where* v)
	{
		Clear<Where>::clear(ptr);
		ptr = v;
		return *this;
	}

	Where* get() const
	{
		return ptr;
	}

	operator Where*() const
	{
		return ptr;
	}

	bool operator !() const
	{
		return !ptr;
	}

	bool hasData() const
	{
		return ptr != NULL;
	}

	Where* operator->() const
	{
		return ptr;
	}

	Where* release()
	{
		Where* tmp = ptr;
		ptr = NULL;
		return tmp;
	}

	void reset(Where* v = NULL)
	{
		if (v != ptr)
		{
			Clear<Where>::clear(ptr);
			ptr = v;
		}
	}

private:
	AutoPtr(AutoPtr&);
	void operator=(AutoPtr&);
};


template <typename Where>
class AutoDispose : public AutoPtr<Where, SimpleDispose>
{
public:
	AutoDispose(Where* v = NULL)
		: AutoPtr<Where, SimpleDispose>(v)
	{ }
};


template <typename Where>
class AutoRelease : public AutoPtr<Where, SimpleRelease>
{
public:
	AutoRelease(Where* v = NULL)
		: AutoPtr<Where, SimpleRelease>(v)
	{ }
};


template <typename T>
class AutoSetRestore
{
public:
	AutoSetRestore(T* aValue, T newValue)
		: value(aValue),
		  oldValue(*aValue)
	{
		*value = newValue;
	}

	~AutoSetRestore()
	{
		*value = oldValue;
	}

private:
	// copying is prohibited
	AutoSetRestore(const AutoSetRestore&);
	AutoSetRestore& operator =(const AutoSetRestore&);

	T* value;
	T oldValue;
};


template <typename T>
class AutoSetRestoreFlag
{
public:
	AutoSetRestoreFlag(T* aValue, T newBit, bool set)
		: value(aValue),
		  bit(newBit),
		  oldValue((*value) & bit)
	{
		if (set)
			*value |= bit;
		else
			*value &= ~bit;
	}

	~AutoSetRestoreFlag()
	{
		*value &= ~bit;
		*value |= oldValue;
	}

private:
	// copying is prohibited
	AutoSetRestoreFlag(const AutoSetRestoreFlag&);
	AutoSetRestoreFlag& operator =(const AutoSetRestoreFlag&);

	T* value;
	T bit;
	T oldValue;
};


template <typename T, typename T2>
class AutoRestore2
{
protected:
	typedef T (T2::*Getter)();
	typedef void (T2::*Setter)(T);

public:
	AutoRestore2(T2* aPointer, Getter aGetter, Setter aSetter)
		: pointer(aPointer),
		  setter(aSetter),
		  oldValue((aPointer->*aGetter)())
	{ }

	void set(T newValue)
	{
		(pointer->*setter)(newValue);
	}

	~AutoRestore2()
	{
		(pointer->*setter)(oldValue);
	}

private:
	// copying is prohibited
	AutoRestore2(const AutoRestore2&);
	AutoRestore2& operator =(const AutoRestore2&);

private:
	T2* pointer;
	Setter setter;
	T oldValue;
};


template <typename T, typename T2>
class AutoSetRestore2 : public AutoRestore2<T, T2>
{
	typedef typename AutoRestore2<T, T2>::Getter Getter;
	typedef typename AutoRestore2<T, T2>::Setter Setter;
public:
	AutoSetRestore2(T2* aPointer, Getter aGetter, Setter aSetter, T newValue)
		: AutoRestore2<T, T2>(aPointer, aGetter, aSetter)
	{
		this->set(newValue);
	}
};


// One more typical class for AutoPtr cleanup
class FileClose
{
public:
	static void clear(FILE *f)
	{
		if (f) {
			fclose(f);
		}
	}
};


} //namespace Firebird

#endif // CLASSES_AUTO_PTR_H

