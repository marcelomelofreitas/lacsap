#ifndef TYPES_H
#define TYPES_H

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <string>

class PrototypeAST;

namespace Types
{
    /* Range is either created by the user, or calculated on basetype */
    class Range
    {
    public:
	Range(int s, int e)
	    : start(s), end(e)
	{
	    assert( ((int64_t)e - (int64_t)s) > 0 && "Range should have start before end.");
	}
    public:
	int GetStart() const { return start; }
	int GetEnd() const { return end; }
	size_t Size() const { return (size_t) ((int64_t)end - (int64_t)start) + 1; }
	void dump() const;
	void DoDump(std::ostream& out) const;
    private:
	int start;
	int end;
    };

    class TypeDecl
    {
    public:
	enum TypeKind
	{
	    TK_Type,
	    TK_Char,
	    TK_Integer,
	    TK_Int64,
	    TK_Real,
	    TK_Void,
	    TK_Array,
	    TK_String,
	    TK_LastArray,
	    TK_Range,
	    TK_Enum,
	    TK_Boolean,
	    TK_Pointer,
	    TK_Field,
	    TK_Record,
	    TK_FuncPtr,
	    TK_Function,
	    TK_File,
	    TK_Set,
	    TK_Variant,
	    TK_Class,
	    TK_MemberFunc,
	};

	TypeDecl(TypeKind k)
	    : kind(k), ltype(0)
	{
	}

	virtual TypeKind Type() const { return kind; }
	virtual ~TypeDecl() { }
	virtual bool isIntegral() const { return false; }
	virtual bool isCompound() const { return false; }
	virtual bool isStringLike() const { return false; }
	virtual bool isUnsigned() const { return false; }
	virtual Range* GetRange() const;
	virtual TypeDecl* SubType() const { return 0; }
	virtual unsigned Bits() const { return 0; }
	virtual bool SameAs(const TypeDecl* ty) const = 0;
	virtual const TypeDecl* CompatibleType(const TypeDecl* ty) const;
	virtual const TypeDecl* AssignableType(const TypeDecl* ty) const { return CompatibleType(ty); }
	llvm::Type* LlvmType() const;
	bool hasLlvmType() { return ltype; }
	void dump(std::ostream& out) const { DoDump(out); }
	void dump() const;
	virtual void DoDump(std::ostream& out) const = 0;
	TypeKind getKind() const { return kind; }
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Type; }
	virtual size_t Size() const;
	size_t AlignSize() const;
    protected:
	virtual llvm::Type* GetLlvmType() const = 0;
    protected:
	const TypeKind kind;
	mutable llvm::Type* ltype;
    };

    class BasicTypeDecl : public TypeDecl
    {
    public:
	using TypeDecl::TypeDecl;
	void DoDump(std::ostream& out) const override;
	bool SameAs(const TypeDecl* ty) const override { return kind == ty->Type(); }
    };

    class CharDecl : public BasicTypeDecl
    {
    public:
	CharDecl() : BasicTypeDecl(TK_Char)
	{
	}
	bool isIntegral() const override { return true; }
	bool isUnsigned() const override { return true; }
	bool isStringLike() const override { return true; }
	const TypeDecl* CompatibleType(const TypeDecl* ty) const override;
	unsigned Bits() const override { return 8; }
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class IntegerDecl : public BasicTypeDecl
    {
    public:
	IntegerDecl()
	    : BasicTypeDecl(TK_Integer) { }
	bool isIntegral() const override { return true; }
	unsigned Bits() const override { return 32; }
	const TypeDecl* CompatibleType(const TypeDecl* ty) const override;
	const TypeDecl* AssignableType(const TypeDecl* ty) const override;
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class Int64Decl : public BasicTypeDecl
    {
    public:
	Int64Decl()
	    : BasicTypeDecl(TK_Int64) { }
	bool isIntegral() const override { return true; }
	unsigned Bits() const override { return 64; }
	const TypeDecl* CompatibleType(const TypeDecl* ty) const override;
	const TypeDecl* AssignableType(const TypeDecl* ty) const override;
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class RealDecl : public BasicTypeDecl
    {
    public:
        RealDecl()
	    : BasicTypeDecl(TK_Real) { }
	const TypeDecl* CompatibleType(const TypeDecl* ty) const override;
	const TypeDecl* AssignableType(const TypeDecl* ty) const override;
	unsigned Bits() const override { return 64; }
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class VoidDecl : public BasicTypeDecl
    {
    public:
	VoidDecl() : BasicTypeDecl(TK_Void)
	{
	}
	const TypeDecl* CompatibleType(const TypeDecl* ty) const override { return 0; }
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class CompoundDecl : public TypeDecl
    {
    public:
	CompoundDecl(TypeKind tk, TypeDecl *b) 
	    : TypeDecl(tk), baseType(b) { }
	bool SameAs(const TypeDecl* ty) const override;
	bool isCompound() const override { return true; }
	TypeDecl* SubType() const override { return baseType; }
	static bool classof(const TypeDecl* e);
    protected:
	TypeDecl* baseType;
    };	

    class SimpleCompoundDecl : public TypeDecl
    {
    public:
	SimpleCompoundDecl(TypeKind k, TypeKind b)
	    : TypeDecl(k), baseType(b) {}
	bool SameAs(const TypeDecl* ty) const override;
	bool isIntegral() const override { return true; }
	TypeKind Type() const override { return baseType; }
	static bool classof(const TypeDecl* e);
    protected:
	llvm::Type* GetLlvmType() const override;
    protected:
	TypeKind baseType;
    };

    class RangeDecl : public SimpleCompoundDecl
    {
    public:
	RangeDecl(Range* r, TypeKind base)
	    : SimpleCompoundDecl(TK_Range, base), range(r)
	{
	    assert(r && "Range should be specified");
	}
    public:
	void DoDump(std::ostream& out) const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Range; }
	bool SameAs(const TypeDecl* ty) const override;
	int GetStart() const { return range->GetStart(); }
	int GetEnd() const { return range->GetEnd(); }
	bool isUnsigned() const override { return GetStart() >= 0; }
	unsigned Bits() const override;
	Range* GetRange() const override { return range; }
	const TypeDecl* CompatibleType(const TypeDecl *ty) const override;
	const TypeDecl* AssignableType(const TypeDecl *ty) const override;
    private:
	Range* range;
    };

    class ArrayDecl : public CompoundDecl
    {
    public:
	ArrayDecl(TypeDecl* b, const std::vector<RangeDecl*>& r)
	    : CompoundDecl(TK_Array, b), ranges(r)
	{
	    assert(r.size() > 0 && "Empty range not allowed");
	}
	ArrayDecl(TypeKind tk, TypeDecl* b, const std::vector<RangeDecl*>& r)
	    : CompoundDecl(tk, b), ranges(r)
	{
	    assert(tk == TK_String && "Expected this to be a string...");
	    assert(r.size() > 0 && "Empty range not allowed");
	}
	const std::vector<RangeDecl*>& Ranges() const { return ranges; }
	bool isStringLike() const override { return (baseType->Type() == TK_Char); }
	void DoDump(std::ostream& out) const override;
	bool SameAs(const TypeDecl* ty) const override;
	static bool classof(const TypeDecl* e)
	{
	    return e->getKind() >= TK_Array && e->getKind() <= TK_LastArray;
	}
    protected:
	llvm::Type* GetLlvmType() const override;
    private:
	std::vector<RangeDecl*> ranges;
    };

    struct EnumValue
    {
	EnumValue(const std::string& nm, int v)
	    : name(nm), value(v) {}
	EnumValue(const EnumValue &e)
	    : name(e.name), value(e.value) {}
	std::string name;
	int value;
    };

    typedef std::vector<EnumValue> EnumValues;

    class EnumDecl : public SimpleCompoundDecl
    {
    public:
	EnumDecl(const std::vector<std::string>& nmv, TypeKind ty = TK_Enum)
	    : SimpleCompoundDecl(TK_Enum, ty)
	{
	    assert(nmv.size() && "Must have names in the enum type.");
	    SetValues(nmv);
	}
    private:
	void SetValues(const std::vector<std::string>& nmv);
    public:
	Range* GetRange() const override { return new Range(0, values.size()-1); }
	const EnumValues& Values() const { return values; }
	bool isUnsigned() const override { return true; }
	void DoDump(std::ostream& out) const override;
	unsigned Bits() const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Enum; }
	bool SameAs(const TypeDecl* ty) const override;
    private:
	EnumValues  values;
    };

    class BoolDecl : public EnumDecl
    {
    public:
	BoolDecl() :
	    EnumDecl(std::vector<std::string>{"false", "true"}, TK_Boolean) { }
    protected:
	llvm::Type* GetLlvmType() const override;
    };


    // Since we need to do "late" binding of pointer types, we just keep
    // the name and resolve the actual type at a later point. If the
    // type is known, store it directly. (Otherwise, when we call the fixup).
    class PointerDecl : public CompoundDecl
    {
    public:
	PointerDecl(const std::string& nm)
	    : CompoundDecl(TK_Pointer, 0), name(nm), incomplete(true) {}
	PointerDecl(TypeDecl* ty)
	    : CompoundDecl(TK_Pointer, ty), name(""), incomplete(false) {}
    public:
	const std::string& Name() { return name; }
	void SetSubType(TypeDecl* t)
	{
	    assert(t && "Type should be non-NULL");
	    baseType = t; 
	    incomplete = false;
	}
	bool IsIncomplete() const { return incomplete; }
	void DoDump(std::ostream& out) const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Pointer; }
    protected:
	llvm::Type* GetLlvmType() const override;
    private:
	std::string name;
	bool incomplete;
    };

    class FunctionDecl : public CompoundDecl
    {
    public:
	FunctionDecl(TypeDecl* resType) 
	    : CompoundDecl(TK_Function, resType)
	{
	}
	void DoDump(std::ostream& out) const override;
	const TypeDecl* CompatibleType(const TypeDecl *ty) const override
	{ return baseType->CompatibleType(ty); }
	const TypeDecl* AssignableType(const TypeDecl *ty) const override 
	{ return baseType->AssignableType(ty); }
    protected:
	llvm::Type* GetLlvmType() const override { return NULL; }
    };

    class FieldDecl : public CompoundDecl
    {
    public:
	enum Access { Private, Protected, Public };
	FieldDecl(const std::string& nm, TypeDecl* ty, bool stat, Access ac = Public)
	    : CompoundDecl(TK_Field, ty), name(nm), isStatic(stat) {}
    public:
	const std::string& Name() const { return name; }
	TypeDecl* FieldType() const { return baseType; }
	void DoDump(std::ostream& out) const override;
	bool isIntegral() const override { return baseType->isIntegral(); }
	bool isCompound() const override { return baseType->isCompound(); }
	bool IsStatic() const { return isStatic; }
	bool SameAs(const TypeDecl* ty) const override { return baseType->SameAs(ty); }
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Field; }
	operator Access() { return access; }
    protected:
	llvm::Type* GetLlvmType() const override;
    private:
	std::string name;
	bool isStatic;
	Access access;
    };

    class FieldCollection : public TypeDecl
    {
    public:
	FieldCollection(TypeKind k, const std::vector<FieldDecl*>& flds)
	    : TypeDecl(k), fields(flds), opaqueType(0) { }
        virtual int Element(const std::string& name) const;
	virtual const FieldDecl* GetElement(unsigned int n) const
	{
	    assert(n < fields.size() && "Out of range field");
	    return fields[n];
	}
	void EnsureSized() const;
	virtual int FieldCount() const { return fields.size(); }
	bool isCompound() const override { return true; }
	bool SameAs(const TypeDecl* ty) const override;
	static bool classof(const TypeDecl* e)
	{
	    return e->getKind() == TK_Variant || e->getKind() == TK_Record || 
		e->getKind() == TK_Class;
	}
    protected:
	std::vector<FieldDecl*> fields;
	mutable llvm::StructType* opaqueType;
    };

    class VariantDecl : public FieldCollection
    {
    public:
	VariantDecl(const std::vector<FieldDecl*>& flds)
	    : FieldCollection(TK_Variant, flds) { };
	void DoDump(std::ostream& out) const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Variant; }
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class RecordDecl : public FieldCollection
    {
    public:
	RecordDecl(const std::vector<FieldDecl*>& flds, VariantDecl* v)
	    : FieldCollection(TK_Record, flds), variant(v) { };
	void DoDump(std::ostream& out) const override;
	size_t Size() const override;
	VariantDecl* Variant() { return variant; }
	bool SameAs(const TypeDecl* ty) const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Record; }
    protected:
	llvm::Type* GetLlvmType() const override;
    private:
	VariantDecl* variant;
    };

    /* Objects can have member functions/procedures */
    class MemberFuncDecl : public TypeDecl
    {
    public:
	enum Flags
	{
	    Static   = 1 << 0,
	    Virtual  = 1 << 1,
	    Override = 1 << 2,
	};
	MemberFuncDecl(PrototypeAST* p, int f)
	    : TypeDecl(TK_MemberFunc), proto(p), flags(f), index(-1) {}

	void DoDump(std::ostream& out) const override;
	bool SameAs(const TypeDecl* ty) const override;
	PrototypeAST* Proto() { return proto; }
	std::string LongName() const { return longname; }
	void LongName(const std::string& name) { longname = name; }
	bool IsStatic() { return flags & Static; }
	bool IsVirtual() { return flags & Virtual; }
	bool IsOverride() { return flags & Override; }
	int VirtIndex() const { return index; }
	void VirtIndex(int n) { index = n; }
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_MemberFunc; }
    protected:
	// We don't actually have an LLVM type for member functions.
	llvm::Type* GetLlvmType() const override { return 0; }
    private:
	PrototypeAST* proto;
	int flags;
	int index;
	std::string longname;
    };

    class ClassDecl : public FieldCollection
    {
    public:
	ClassDecl(const std::string& nm, const std::vector<FieldDecl*>& flds, 
		  const std::vector<MemberFuncDecl*> mf, VariantDecl* v, ClassDecl* base);

	void DoDump(std::ostream& out) const override;
        int Element(const std::string& name) const override;
	const FieldDecl* GetElement(unsigned int n) const override;
	const FieldDecl* GetElement(unsigned int n, std::string& objname) const;
        int FieldCount() const override;
	size_t Size() const override;
	VariantDecl* Variant() { return variant; }
	bool SameAs(const TypeDecl* ty) const override;
	size_t MembFuncCount() const;
	int MembFunc(const std::string& nm) const;
	MemberFuncDecl* GetMembFunc(size_t index) const;
	size_t NumVirtFuncs() const;
	std::string Name() const { return name; }
	const TypeDecl* CompatibleType(const TypeDecl *ty) const override;
	llvm::Type* VTableType(bool opaque) const;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Class; }
    protected:
	llvm::Type* GetLlvmType() const override;
    private:
	ClassDecl* baseobj;
	std::string name;
	VariantDecl* variant;
	std::vector<MemberFuncDecl*> membfuncs;
	mutable llvm::StructType* vtableType;
    };

    class FuncPtrDecl : public CompoundDecl
    {
    public:
	FuncPtrDecl(PrototypeAST* func);
	void DoDump(std::ostream& out) const override;
	PrototypeAST* Proto() const { return proto; }
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_FuncPtr; }
	bool SameAs(const TypeDecl* ty) const override;
    protected:
	llvm::Type* GetLlvmType() const override;
    private:
	PrototypeAST* proto;
	TypeDecl*     baseType;
    };

    class FileDecl : public CompoundDecl
    {
    public:
	enum
	{
	    Handle,
	    Buffer,
	} FileFields;
	FileDecl(TypeDecl* ty)
	    : CompoundDecl(TK_File, ty) {}
	void DoDump(std::ostream& out) const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_File; }
    protected:
	llvm::Type* GetLlvmType() const override;
    protected:
    };

    class TextDecl : public FileDecl
    {
    public:
	TextDecl()
	    : FileDecl(new CharDecl) {}
	void DoDump(std::ostream& out) const override;
    protected:
	llvm::Type* GetLlvmType() const override;
    };

    class SetDecl : public CompoundDecl
    {
    public:
	typedef unsigned int ElemType;
	// Must match with "runtime".
	enum {
	    MaxSetWords = 16,
	    SetBits = 32,
	    MaxSetSize = MaxSetWords * SetBits,
	    SetMask = SetBits-1,
	    SetPow2Bits = 5
	};
	SetDecl(RangeDecl* r, TypeDecl *ty);
	void DoDump(std::ostream& out) const override;
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_Set; }
	size_t SetWords() const { return (range->GetRange()->Size() + SetMask) >> SetPow2Bits; }
	Range* GetRange() const override;
	void UpdateRange(RangeDecl* r) { range = r; }
	void UpdateSubtype(TypeDecl* ty);
	bool SameAs(const TypeDecl* ty) const override;
    private:
	llvm::Type* GetLlvmType() const override;
    private:
	RangeDecl* range;
    };

    class StringDecl : public ArrayDecl
    {
    public:
	StringDecl(unsigned size)
	    : ArrayDecl(TK_String, new CharDecl, 
			std::vector<RangeDecl*>(1, new RangeDecl(new Range(0, size), TK_Integer)))
	{
	    assert(size > 0 && "Zero size not allowed");
	}
	static bool classof(const TypeDecl* e) { return e->getKind() == TK_String; }
	bool isStringLike() const override { return true; }
	void DoDump(std::ostream& out) const override;
	const TypeDecl* CompatibleType(const TypeDecl *ty) const override;
    };

    llvm::Type* GetType(TypeDecl::TypeKind type);
    llvm::Type* GetVoidPtrType();
    llvm::Type* GetFileType(const std::string& name, TypeDecl* baseType);
    TypeDecl* GetVoidType();
    TextDecl* GetTextType();
    StringDecl* GetStringType();
} // Namespace Types

bool operator==(const Types::TypeDecl& lty, const Types::TypeDecl& rty);
inline bool operator!=(const Types::TypeDecl& lty, const Types::TypeDecl& rty) { return !(lty == rty); }

bool operator==(const Types::Range& a, const Types::Range& b);
inline bool operator!=(const Types::Range& a, const Types::Range& b) { return !(b == a); }

bool operator==(const Types::EnumValue& a, const Types::EnumValue& b);
inline bool operator!=(const Types::EnumValue& a, const Types::EnumValue& b) { return !(a == b); }


#endif
