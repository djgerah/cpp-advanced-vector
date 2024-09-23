#pragma once
#include <algorithm>
#include <cassert>
#include <memory>
#include <new>

/*
    Согласно идиоме RAII, жизненный цикл ресурса, который программа получает во временное пользование, должен привязываться ко времени жизни объекта. 
    Создав объект, который владеет некоторым ресурсом, программа может использовать этот ресурс на протяжении жизни объекта.
    Класс Vector владеет несколькими типами ресурсов:
    сырая память, которую класс запрашивает, используя Allocate, и освобождает, используя Deallocate;
    элементы вектора, которые создаются размещающим оператором new и удаляются вызовом деструктора.
    Выделив код, управляющий сырой памятью, в отдельный класс-обёртку, вы возможно упростить класс Vector.
    Шаблонный класс RawMemory будет отвечать за хранение буфера, который вмещает заданное количество элементов, и предоставлять доступ к элементам по индексу:
*/

template <typename T>
class RawMemory 
{
    public:

        RawMemory() = default;

        explicit RawMemory(size_t capacity)
            : buffer_(Allocate(capacity))
            , capacity_(capacity) 
            {}

        RawMemory(const RawMemory&) = delete;
        
        RawMemory& operator=(const RawMemory& other) = delete;
        
        RawMemory(RawMemory&& other) noexcept 
        {
            buffer_ = other.buffer_;
            capacity_ = other.capacity_;
            other.buffer_ = nullptr;
            other.capacity_ = 0;
        }

        RawMemory& operator=(RawMemory&& other) noexcept 
        {
            if (this != &other) 
            {
                Swap(other);
                Deallocate(other.buffer_);
                other.capacity_ = 0;
            }
            return *this;
        }

        ~RawMemory() 
        {
            Deallocate(buffer_);
        }

        T* operator+(size_t offset) noexcept 
        {
            // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
            assert(offset <= capacity_);
            return buffer_ + offset;
        }

        const T* operator+(size_t offset) const noexcept 
        {
            return const_cast<RawMemory&>(*this) + offset;
        }

        const T& operator[](size_t index) const noexcept 
        {
            return const_cast<RawMemory&>(*this)[index];
        }

        T& operator[](size_t index) noexcept 
        {
            assert(index < capacity_);
            return buffer_[index];
        }

        void Swap(RawMemory& other) noexcept 
        {
            std::swap(buffer_, other.buffer_);
            std::swap(capacity_, other.capacity_);
        }

        const T* GetAddress() const noexcept 
        {
            return buffer_;
        }

        T* GetAddress() noexcept 
        {
            return buffer_;
        }

        size_t Capacity() const 
        {
            return capacity_;
        }

    private:

    /*
        Вспомогательные статические методы Allocate и Deallocate, чтобы выделить и освободить сырую память. 
        Для этого используем функции operator new и operator delete
        Функция operator delete не выбрасывает исключений. Это же требование можно предъявить и к шаблонному параметру T, 
        поэтому смело объявляйте функции Destroy и Deallocate как noexcept
    */

        // Выделяет сырую память под n элементов и возвращает указатель на неё
        static T* Allocate(size_t n) 
        {
            return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
        }

        // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
        static void Deallocate(T* buf) noexcept 
        {
            operator delete(buf);
        }

        T* buffer_ = nullptr;
        size_t capacity_ = 0;
};

template <typename T>
class Vector 
{
    public:

        // Конструктор по умолчанию. Инициализирует вектор нулевого размера и вместимости. 
        // Не выбрасывает исключений. Алгоритмическая сложность: O(1)
        Vector() = default;

    /*
        Этот конструктор сначала выделяет в сырой памяти буфер, достаточный для хранения  элементов в количестве, равном size. 
        Затем конструирует в сырой памяти элементы массива. Для этого он вызывает их конструктор по умолчанию, 
        используя размещающий оператор new.
    */

        // Конструктор, который создаёт вектор заданного размера. Вместимость созданного вектора равна его размеру, 
        // а элементы проинициализированы значением по умолчанию для типа T. Алгоритмическая сложность: O(размер вектора)
        explicit Vector(size_t size)
            : data_(size)
            , size_(size)
            {   
                // В стандартной библиотеке есть целое семейство функций, создающих и удаляющих группы объектов 
                // в неинициализированной области памяти. Объявлены они в файле <memory>
                std::uninitialized_value_construct_n(begin(), size);
            }

        // Копирующий конструктор. Создаёт копию элементов исходного вектора. Имеет вместимость, равную размеру исходного вектора, 
        // то есть выделяет память без запаса. Алгоритмическая сложность: O(размер исходного вектора).
        Vector(const Vector& other)
            : data_(other.size_)
            , size_(other.size_)
            {
                // Аналогично функция uninitialized_copy_n упрощает конструктор копирования до одной строки
                std::uninitialized_copy_n(other.begin(), size_, begin());
            }

        Vector(Vector&& other) noexcept 
        {
            Swap(other);
        }

    /*
        Для корректного разрушения контейнера Vector нужно сначала вызвать destroy_n, передав ей указатель data_ 
        и количество элементов size_
    */

        // Деструктор. Разрушает содержащиеся в векторе элементы и освобождает занимаемую ими память. 
        // Алгоритмическая сложность: O(размер вектора)
        ~Vector() 
        {   
            // Функция std::destroy_n заменяет метод DestroyN
            std::destroy_n(begin(), size_);
        }

        Vector& operator=(const Vector& other) 
        {
            if (this != &other) 
            {
        /*
            Применяем идиому copy-and-swap. Создаем копию объекта. Изменяем копию. 
            При этом оригинальные объекты остаются нетронутыми
            Если все изменения прошли успешно, заменяем оригинальный объект измененной копией. 
            Если же при изменении копии на каком-то этапе возникла ошибка, то оригинальный объект не заменяется.
        */
                if (other.size_ > data_.Capacity()) 
                {
                    Vector copy(other);
                    Swap(copy);
                }

                else 
                {   
                    size_t min_size = std::min(size_, other.size_);
                    std::copy_n(other.begin(), min_size, begin());

                    if (other.size_ < size_) 
                    {
                        // Если размер other меньше размера this, необходимо скопировать элементы из other и удалить существующие, которые не нужны
                        std::destroy_n(begin() + other.size_, size_ - other.size_);
                    } 
                    
                    else 
                    {
                        // Если размер other больше размера this, необходимо скопировать элементы из other и проинициализировать новые    
                        std::uninitialized_copy_n(other.data_ + size_, other.size_ - size_, data_ + size_);
                    }

                    size_ = other.size_;
                }
            }
            return *this;
        }

        Vector& operator=(Vector&& rhs) noexcept 
        {
            if (this != &rhs) 
            {
                Swap(rhs);
            }
            return *this;
        }

        // В константном операторе [] используется оператор  const_cast, чтобы снять константность с ссылки 
        // на текущий объект и вызвать неконстантную версию оператора [].
        // Так получится избавиться от дублирования проверки assert(index < size)
        const T& operator[](size_t index) const noexcept 
        {
            return const_cast<Vector&>(*this)[index];
        }

        T& operator[](size_t index) noexcept 
        {
            assert(index < size_);
            return data_[index];
        }

        void Swap(Vector& other) noexcept 
        {
            data_.Swap(other.data_);
            std::swap(size_, other.size_);
        }

        // Класс имеет вспомогательные методы Size и Capacity, а также две версии оператора [] для доступа к элементам вектора
        size_t Size() const noexcept 
        {
            return size_;
        }

        size_t Capacity() const noexcept 
        {
            return data_.Capacity();
        }

        using iterator = T*;
        using const_iterator = const T*;
        
        iterator begin() noexcept 
        {
            return data_.GetAddress();
        }

        iterator end() noexcept 
        {
            return data_.GetAddress() + size_;
        }

        const_iterator begin() const noexcept
        {
            return cbegin();
        }

        const_iterator end() const noexcept
        {
            return cend();
        }

        const_iterator cbegin() const noexcept
        {
            return data_.GetAddress();
        }

        const_iterator cend() const noexcept
        {
            return data_.GetAddress() + size_;
        }

   /*
        Метод Reserve предназначен для заблаговременного резервирования памяти под элементы вектора, 
        когда известно их примерное количество.
   */ 

        // Резервирует достаточно места, чтобы вместить количество элементов, равное capacity. 
        // Если новая вместимость не превышает текущую, метод не делает ничего. Алгоритмическая сложность: O(размер вектора)
        void Reserve(size_t new_capacity) 
        {
            if (new_capacity <= data_.Capacity())
            {
                return;
            }
            
            RawMemory<T> new_data(new_capacity);

    /*
        Элементы перемещаются, только если соблюдается хотя бы одно из условий:
        1. Конструктор перемещения типа T не выбрасывает исключений;
        2. Тип T не имеет копирующего конструктора.
        В противном случае элементы надо копировать. 
    */

            // constexpr оператор if будет вычислен во время компиляции
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) 
            {
                std::uninitialized_move_n(begin(), size_, new_data.GetAddress());
            }

            else 
            {
                // Конструируем элементы в new_data, копируя их из data_
                std::uninitialized_copy_n(begin(), size_, new_data.GetAddress());
            }
            // Разрушаем элементы в data_
            std::destroy_n(begin(), size_);
            // Избавляемся от старой сырой памяти, обменивая её на новую
            data_.Swap(new_data);
            // При выходе из метода старая память будет возвращена в кучу
        }

    /*
        Сложность метода Resize должна линейно зависеть от разницы между текущим и новым размером вектора. 
        Если новый размер превышает текущую вместимость вектора, 
        сложность операции может дополнительно линейно зависеть от текущего размера вектора.
    */

        void Resize(size_t new_size) 
        {
            // увеличение размера вектора
            if (new_size > size_) 
            {
                // Когда метод Resize должен увеличить размер вектора, сначала нужно убедиться, 
                // что вектору достаточно памяти для новых элементов, вызвав метод Reserve 
                Reserve(new_size);
                // Затем новые элементы нужно проинициализировать, используя функцию uninitialized_value_construct_n
                std::uninitialized_value_construct_n(begin() + size_, new_size - size_);
            } 
            // уменьшение размера вектора
            else 
            {
                // При уменьшении размера вектора нужно удалить лишние элементы вектора, вызвав их деструкторы
                std::destroy_n(begin() + new_size, size_ - new_size);
            }
             // В конце, нужно изменить размер вектора
            size_ = new_size;
        }

        // EmplaceBack используется для реализации обоих методов PushBack
        void PushBack(const T& value) 
        {
            EmplaceBack(value);
        }

        void PushBack(T&& value) 
        {
            EmplaceBack(std::move(value));
        }
    
        // Метод PopBack разрушает последний элемент вектора и уменьшает размер вектора на единицу,
        // он не должен выбрасывать исключений при вызове у непустого вектора.
        // Как и в случае стандартного вектора, вызов PopBack на пустом векторе приводит к неопределённому поведению.
        // Метод PopBack должен иметь сложность O(1)
        void PopBack() /* noexcept */ 
        {
            if (size_ > 0) 
            {
                std::destroy_at(end() - 1);
                --size_;
            }
        }

    /*
        Конструкторы разных классов различаются количеством и типом своих аргументов. 
        Метод EmplaceBack должен уметь вызывать любые конструкторы типа T, 
        передавая им как константные и неконстантные ссылки, так и rvalue-ссылки на временные объекты. 
        Для этого EmplaceBack должен быть вариативным шаблоном, который принимает аргументы конструктора T по Forwarding-ссылкам:
        Метод EmplaceBack возвращает ссылку на добавленный элемент вектора. Это удобно для использования объекта сразу после его конструирования.
        Метод должен выполняться за время O(1)
    */

    template <typename... Args>
    T& EmplaceBack(Args&&... args) 
    {

    /*
        Реализация похожа на PushBack, только вместо копирования или перемещения
        переданного элемента, он конструируется путём передачи параметров метода конструктору T
    */

        Emplace(end(), std::forward<Args>(args)...);

        return data_[size_ - 1];
    }

    // Методы Insert возвращает итератор, указывающий на вставленный элемент в новом блоке памяти.
    iterator Insert(const_iterator pos, const T& value) 
    {
        return Emplace(pos, value);
    }

    // Так как Emplace способен передать свои аргументы любому конструктору T, 
    // включая конструкторы копирования и перемещения, оба метода Insert можно реализовать на основе Emplace.
    iterator Insert(const_iterator pos, T&& value) 
    {
        return Emplace(pos, std::move(value));
    }

    template<typename... Args> 
    iterator Emplace(const_iterator pos, Args&&... args)  
    { 
        size_t index = std::distance(cbegin(), pos); // Итератор pos указывает на позицию вставки элемента 
        // Если вектор имеет достаточную вместимость для вставки ещё одного элемента 
        if (size_ < data_.Capacity())  
        {
            // Реаллокация не требуется
            NoRealloc(index, std::forward<Args>(args)...);
        } 
        // Иначе
        else 
        {   
            // Необходима реаллокация
            Realloc(index, std::forward<Args>(args)...);
        }

        // В конце остаётся обновить размер вектора и вернуть итератор, указывающий на вставленный элемент 
        size_++; 
 
        return data_ + index; 
    } 

    template<typename... Args>
    void NoRealloc(size_t index, Args&&... args) 
    {
        // Сначала значение копируется или перемещается во временный объект 
        T temp{std::forward<Args>(args)...}; 
        // Прежде чем вставить элемент в середину вектора, для него освобождается место.  
        // Сначала в неинициализированной области, следующей за последним элементом,  
        // создается копия или перемещается значение последнего элемента вектора 
        new (end()) T{std::move(data_[size_ - 1])}; 
        // Затем перемещаются элементы диапазона [pos, end() - 1) вправо на один элемент. 
        // Для сдвига элементов вправо используется функция move_backward. Она перемещает объекты, начиная с последнего 
        std::move_backward(begin() + index, end() - 1, end()); 
        // После перемещения элементов временное значение перемещается во вставляемую позицию 
        data_[index] = std::move(temp); 
    }

    template<typename... Args>
    void Realloc(size_t index, Args&&... args) 
    {    
        // На первом шаге нужно выделить новый блок сырой памяти с удвоенной вместимостью (если size != 0) 
        // Затем сконструировать в ней вставляемый элемент, используя конструктор копирования или перемещения 
        RawMemory<T> new_data{size_ == 0 ? 1 : size_ * 2}; 
        new (new_data + index) T{std::forward<Args>(args)...}; 

        // Вставляемый элемент конструируется в новом блоке памяти до конструирования остальных элементов. 
        // Затем перемещаются либо копируются элементы, которые предшествуют вставленному элементу: 
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>)  
        { 
            std::uninitialized_move_n(begin() + index, size_ - index, new_data.GetAddress() + index + 1); 
            std::uninitialized_move_n(begin(), index, new_data.GetAddress()); 
        }  

        else  
        { 
            std::uninitialized_copy_n(begin() + index, size_ - index, new_data.GetAddress() + index + 1); 
            std::uninitialized_copy_n(begin(), index, new_data.GetAddress()); 
        } 

        std::destroy_n(begin(), size_ - index); 
        data_.Swap(new_data); 
    } 

    // Метод Erase удаляет элемент, на который указывает переданный итератор
    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if (pos < cend()) 
        {
            size_t index = std::distance(cbegin(), pos);
            // На место удаляемого элемента нужно переместить следующие за ним элементы
            std::move(data_ + index + 1, end(), data_ + index);
            // После разрушения объекта в конце вектора и обновления поля size_ удаление элемента можно считать завершённым
            PopBack();
            // Метод Erase возвращает итератор, который ссылается на элемент, следующий за удалённым. 
            return data_ + index;
        }

        else 
        {   
            PopBack();
            // Если удалялся последний элемент вектора, возвращает end-итератор
            return end();
        }
    }

    private:

        RawMemory<T> data_;
        size_t size_ = 0;
};