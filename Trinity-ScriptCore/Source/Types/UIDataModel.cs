using System;
using System.Text;
using Trinity.Interop;

namespace Trinity
{
    // What a data model's value holds, numbered as the engine's UIDataType
    public enum UIDataType
    {
        Int,
        Float,
        Bool,
        String
    }

    // A field of each row of a list, by the name an expression reaches it by, as row.name
    public readonly struct UIDataField
    {
        public UIDataField(string name, UIDataType type)
        {
            Name = name;
            Type = type;
        }

        public string Name { get; }
        public UIDataType Type { get; }
    }

    // A data model of a UIDocument, by the name its document gives in data-model="name", which the scene's scripts bind values and lists into, kept while the scene's scripts run, and given to every document its entity shows. Each name is bound once: binding it again gives the value or list bound before, with what it holds, and refuses one of another type or kind, logged. A name an expression reads before it is bound reads as nothing, so a document reads before its scripts bind. A name is letters, digits and underscores, not starting with a digit. Main thread only
    public readonly unsafe struct UIDataModel : IEquatable<UIDataModel>
    {
        internal UIDataModel(ulong handle)
        {
            Handle = handle;
        }

        internal ulong Handle { get; }

        public bool IsValid => Handle != 0 && Native.UIDataModel_IsValid(Handle);

        public UIDataValue BindInt(string name, int value = 0) => Bind(name, UIDataType.Int, value, 0.0f, null);

        public UIDataValue BindFloat(string name, float value = 0.0f) => Bind(name, UIDataType.Float, 0, value, null);

        public UIDataValue BindBool(string name, bool value = false) => Bind(name, UIDataType.Bool, value ? 1 : 0, 0.0f, null);

        public UIDataValue BindString(string name, string value = "") => Bind(name, UIDataType.String, 0, 0.0f, value);

        // The fields' names cross together, each as a slice of one buffer
        public UIDataList BindList(string name, params UIDataField[] fields)
        {
            ArgumentNullException.ThrowIfNull(fields);
            int l_Length = 0;
            foreach (UIDataField it_Field in fields)
            {
                l_Length += Encoding.UTF8.GetByteCount(it_Field.Name ?? string.Empty);
            }

            byte[] l_Bytes = new byte[Math.Max(l_Length, 1)];
            Span<Utf8> l_Names = fields.Length <= 32 ? stackalloc Utf8[fields.Length] : new Utf8[fields.Length];
            Span<int> l_Types = fields.Length <= 32 ? stackalloc int[fields.Length] : new int[fields.Length];
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Bytes)
            fixed (byte* l_NameData = l_Name.Bytes)
            fixed (Utf8* l_NamesData = l_Names)
            fixed (int* l_TypesData = l_Types)
            {
                int l_At = 0;
                for (int it_Field = 0; it_Field < fields.Length; ++it_Field)
                {
                    string l_FieldName = fields[it_Field].Name ?? string.Empty;
                    int l_Count = Encoding.UTF8.GetBytes(l_FieldName, 0, l_FieldName.Length, l_Bytes, l_At);
                    l_Names[it_Field] = new Utf8(l_Data + l_At, l_Count);
                    l_Types[it_Field] = (int)fields[it_Field].Type;
                    l_At += l_Count;
                }

                return new UIDataList(Native.UIDataModel_BindList(Handle, new Utf8(l_NameData, l_Name.Bytes.Length), l_NamesData, l_TypesData, fields.Length));
            }
        }

        public bool Equals(UIDataModel other) => Handle == other.Handle;

        public override bool Equals(object? other) => other is UIDataModel l_Model && Equals(l_Model);

        public override int GetHashCode() => Handle.GetHashCode();

        private UIDataValue Bind(string name, UIDataType type, int integer, float real, string? text)
        {
            using Utf8String l_Name = new(name, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Text = new(text, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_NameData = l_Name.Bytes)
            fixed (byte* l_TextData = l_Text.Bytes)
            {
                return new UIDataValue(Native.UIDataModel_BindValue(Handle, new Utf8(l_NameData, l_Name.Bytes.Length), (int)type, integer, real, new Utf8(l_TextData, l_Text.Bytes.Length)));
            }
        }
    }

    // A value a script bound into a data model, which {{ name }} and data-if, data-class and data-value read, safe to keep after its model has gone, when it does nothing and reads as nothing. Setting it to what it holds does nothing, so a value set every frame costs the UI nothing until it changes, and setting it allocates nothing, text included, which crosses from the stack. A control's data-value or data-checked writes into it as the player changes the control. A value of another type is refused, logged once. Main thread only
    public readonly unsafe struct UIDataValue : IEquatable<UIDataValue>
    {
        internal UIDataValue(ulong handle)
        {
            Handle = handle;
        }

        internal ulong Handle { get; }

        public bool IsValid => Handle != 0 && Native.UIDataValue_GetType(Handle) >= 0;

        // Int for one that is not valid
        public UIDataType Type => (UIDataType)Math.Max(Native.UIDataValue_GetType(Handle), 0);

        public void Set(int value) => Native.UIDataValue_Set(Handle, (int)UIDataType.Int, value, 0.0f, default);

        public void Set(float value) => Native.UIDataValue_Set(Handle, (int)UIDataType.Float, 0, value, default);

        public void Set(bool value) => Native.UIDataValue_Set(Handle, (int)UIDataType.Bool, value ? 1 : 0, 0.0f, default);

        public void Set(string value)
        {
            using Utf8String l_Text = new(value, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_Data = l_Text.Bytes)
            {
                Native.UIDataValue_Set(Handle, (int)UIDataType.String, 0, 0.0f, new Utf8(l_Data, l_Text.Bytes.Length));
            }
        }

        // Each the type's default for a value of another type, or none
        public int GetInt()
        {
            int l_Integer = 0;
            float l_Real = 0.0f;
            Utf8 l_Text = default;

            return Native.UIDataValue_Get(Handle, (int)UIDataType.Int, &l_Integer, &l_Real, &l_Text) ? l_Integer : 0;
        }

        public float GetFloat()
        {
            int l_Integer = 0;
            float l_Real = 0.0f;
            Utf8 l_Text = default;

            return Native.UIDataValue_Get(Handle, (int)UIDataType.Float, &l_Integer, &l_Real, &l_Text) ? l_Real : 0.0f;
        }

        public bool GetBool()
        {
            int l_Integer = 0;
            float l_Real = 0.0f;
            Utf8 l_Text = default;

            return Native.UIDataValue_Get(Handle, (int)UIDataType.Bool, &l_Integer, &l_Real, &l_Text) && l_Integer != 0;
        }

        public string GetString()
        {
            int l_Integer = 0;
            float l_Real = 0.0f;
            Utf8 l_Text = default;

            return Native.UIDataValue_Get(Handle, (int)UIDataType.String, &l_Integer, &l_Real, &l_Text) ? l_Text.ToString() : string.Empty;
        }

        public bool Equals(UIDataValue other) => Handle == other.Handle;

        public override bool Equals(object? other) => other is UIDataValue l_Value && Equals(l_Value);

        public override int GetHashCode() => Handle.GetHashCode();
    }

    // A list a script bound into a data model, which data-for walks, as data-for="row : scores" with {{ row.name }}, and whose rows count as scores.size. Rows are set field by field, by the field's name, as values are set, and rows added by Resize hold 0, false and empty text until set. A row past the end, or a field the list has not, is refused, logged once. Main thread only
    public readonly unsafe struct UIDataList : IEquatable<UIDataList>
    {
        internal UIDataList(ulong handle)
        {
            Handle = handle;
        }

        internal ulong Handle { get; }

        public bool IsValid => Handle != 0 && Native.UIDataList_IsValid(Handle);

        public int Count => Native.UIDataList_GetSize(Handle);

        public void Resize(int rows)
        {
            ArgumentOutOfRangeException.ThrowIfNegative(rows);
            Native.UIDataList_Resize(Handle, rows);
        }

        public void Set(int row, string field, int value) => SetCell(row, field, UIDataType.Int, value, 0.0f, null);

        public void Set(int row, string field, float value) => SetCell(row, field, UIDataType.Float, 0, value, null);

        public void Set(int row, string field, bool value) => SetCell(row, field, UIDataType.Bool, value ? 1 : 0, 0.0f, null);

        public void Set(int row, string field, string value) => SetCell(row, field, UIDataType.String, 0, 0.0f, value);

        public int GetInt(int row, string field) => GetCell(row, field, UIDataType.Int, out int l_Integer, out _, out _) ? l_Integer : 0;

        public float GetFloat(int row, string field) => GetCell(row, field, UIDataType.Float, out _, out float l_Real, out _) ? l_Real : 0.0f;

        public bool GetBool(int row, string field) => GetCell(row, field, UIDataType.Bool, out int l_Integer, out _, out _) && l_Integer != 0;

        public string GetString(int row, string field) => GetCell(row, field, UIDataType.String, out _, out _, out string? l_Text) ? l_Text! : string.Empty;

        public bool Equals(UIDataList other) => Handle == other.Handle;

        public override bool Equals(object? other) => other is UIDataList l_List && Equals(l_List);

        public override int GetHashCode() => Handle.GetHashCode();

        private void SetCell(int row, string field, UIDataType type, int integer, float real, string? text)
        {
            using Utf8String l_Field = new(field, stackalloc byte[Utf8String.c_StackBytes]);
            using Utf8String l_Text = new(text, stackalloc byte[Utf8String.c_StackBytes]);
            fixed (byte* l_FieldData = l_Field.Bytes)
            fixed (byte* l_TextData = l_Text.Bytes)
            {
                Native.UIDataList_Set(Handle, row, new Utf8(l_FieldData, l_Field.Bytes.Length), (int)type, integer, real, new Utf8(l_TextData, l_Text.Bytes.Length));
            }
        }

        private bool GetCell(int row, string field, UIDataType type, out int integer, out float real, out string? text)
        {
            int l_Integer = 0;
            float l_Real = 0.0f;
            Utf8 l_Text = default;
            bool l_Found;
            using (Utf8String l_Field = new(field, stackalloc byte[Utf8String.c_StackBytes]))
            {
                fixed (byte* l_FieldData = l_Field.Bytes)
                {
                    l_Found = Native.UIDataList_Get(Handle, row, new Utf8(l_FieldData, l_Field.Bytes.Length), (int)type, &l_Integer, &l_Real, &l_Text);
                }
            }

            integer = l_Integer;
            real = l_Real;
            text = l_Found && type == UIDataType.String ? l_Text.ToString() : null;

            return l_Found;
        }
    }
}