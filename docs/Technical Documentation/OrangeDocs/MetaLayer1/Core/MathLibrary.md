# Orange引擎数学库

## 概述

Orange引擎数学库是一个全面的数学工具集，为3D图形和游戏开发提供必要的数学功能。该库基于GLM（OpenGL Mathematics）库进行封装，提供了友好的API，同时隐藏了第三方库的具体实现细节。

## 特性

- 完整的向量运算支持（Vector2, Vector3, Vector4）
- 矩阵变换（Matrix3, Matrix4）
- 四元数旋转（Quaternion）
- 常用几何数学函数
- 图形学相关数学工具

## 命名空间

所有数学库相关的类和函数都位于 `Orange::Math` 命名空间中。

```cpp
using namespace Orange::Math;
```

## 向量类

### Vector2

二维向量，表示2D空间中的点或方向。

#### 基本用法

```cpp
// 创建向量
Vector2 a;                 // (0, 0)
Vector2 b(1.0f, 2.0f);     // (1, 2)
Vector2 c = Vector2::One(); // (1, 1)

// 访问分量
float x = b.X();  // 1.0
float y = b.Y();  // 2.0

// 设置分量
b.SetX(3.0f);     // b现在是(3, 2)

// 向量运算
Vector2 sum = a + b;          // 向量加法
Vector2 diff = b - a;         // 向量减法
Vector2 scaled = b * 2.0f;    // 标量乘法
float length = b.Length();    // 向量长度
float lenSq = b.LengthSquared(); // 长度平方
Vector2 normalized = b.Normalized(); // 单位向量
b.Normalize();                // 原地单位化

// 点积
float dot = a.Dot(b);

// 其他操作
Vector2 reflected = a.Reflect(b);       // 反射
Vector2 interpolated = a.Lerp(b, 0.5f); // 线性插值
float distance = a.Distance(b);         // 距离
```

#### 常用静态方法

```cpp
Vector2 zero = Vector2::Zero();     // (0, 0)
Vector2 one = Vector2::One();       // (1, 1)
Vector2 unitX = Vector2::UnitX();   // (1, 0)
Vector2 unitY = Vector2::UnitY();   // (0, 1)
```

### Vector3

三维向量，表示3D空间中的点或方向。

#### 基本用法

```cpp
// 创建向量
Vector3 a;                        // (0, 0, 0)
Vector3 b(1.0f, 2.0f, 3.0f);      // (1, 2, 3)
Vector3 c = Vector3::One();       // (1, 1, 1)
Vector3 d(Vector2(1.0f, 2.0f), 3.0f); // 从Vector2创建

// 访问分量
float x = b.X();  // 1.0
float y = b.Y();  // 2.0
float z = b.Z();  // 3.0
Vector2 xy = b.XY(); // 获取xy分量

// 向量运算
Vector3 cross = a.Cross(b);       // 叉积
Vector3 projected = a.Project(b); // 投影
Vector3 refracted = a.Refract(b, 1.5f); // 折射 (eta=1.5)
```

#### 常用静态方法

```cpp
Vector3 zero = Vector3::Zero();     // (0, 0, 0)
Vector3 one = Vector3::One();       // (1, 1, 1)
Vector3 unitX = Vector3::UnitX();   // (1, 0, 0)
Vector3 unitY = Vector3::UnitY();   // (0, 1, 0)
Vector3 unitZ = Vector3::UnitZ();   // (0, 0, 1)
Vector3 up = Vector3::Up();         // (0, 1, 0)
Vector3 down = Vector3::Down();     // (0, -1, 0)
Vector3 right = Vector3::Right();   // (1, 0, 0)
Vector3 left = Vector3::Left();     // (-1, 0, 0)
Vector3 forward = Vector3::Forward(); // (0, 0, 1)
Vector3 backward = Vector3::Backward(); // (0, 0, -1)
```

### Vector4

四维向量，通常用于表示齐次坐标或颜色（RGBA）。

#### 基本用法

```cpp
// 创建向量
Vector4 a;                           // (0, 0, 0, 0)
Vector4 b(1.0f, 2.0f, 3.0f, 4.0f);   // (1, 2, 3, 4)
Vector4 c = Vector4::One();          // (1, 1, 1, 1)
Vector4 d(Vector3(1.0f, 2.0f, 3.0f), 4.0f); // 从Vector3创建

// 访问分量
float w = b.W();       // 4.0
Vector3 xyz = b.XYZ(); // 获取xyz分量
```

## 矩阵类

### Matrix3

3x3矩阵，主要用于2D变换、3D旋转和缩放。

#### 基本用法

```cpp
// 创建矩阵
Matrix3 a;                     // 单位矩阵
Matrix3 b = Matrix3::Identity(); // 单位矩阵
Matrix3 c = Matrix3::Rotation(Math::PI / 4.0f); // 旋转矩阵(45度)
Matrix3 d = Matrix3::Scale(Vector2(2.0f, 3.0f)); // 缩放矩阵

// 访问元素
float element = c.Get(0, 0);  // 第0行第0列元素
c.Set(0, 1, 5.0f);           // 设置第0行第1列元素为5

// 访问行和列
Vector3 row1 = c.GetRow(1);    // 获取第1行
Vector3 col2 = c.GetColumn(2); // 获取第2列
c.SetRow(0, Vector3(1.0f, 0.0f, 0.0f)); // 设置第0行
c.SetColumn(1, Vector3(0.0f, 1.0f, 0.0f)); // 设置第1列

// 矩阵操作
Matrix3 transposed = c.Transpose(); // 转置
Matrix3 inverted = c.Inverse();     // 求逆
float det = c.Determinant();        // 行列式

// 矩阵运算
Matrix3 sum = a + b;           // 矩阵加法
Matrix3 product = a * b;       // 矩阵乘法
Vector3 transformed = a * Vector3(1.0f, 2.0f, 3.0f); // 向量变换
```

### Matrix4

4x4矩阵，用于3D变换，包括平移、旋转、缩放和投影。

#### 基本用法

```cpp
// 创建矩阵
Matrix4 a;                     // 单位矩阵
Matrix4 t = Matrix4::Translation(Vector3(1.0f, 2.0f, 3.0f)); // 平移矩阵
Matrix4 r = Matrix4::Rotation(Vector3(0.0f, Math::PI/2, 0.0f)); // 旋转矩阵(Y轴旋转90度)
Matrix4 s = Matrix4::Scale(Vector3(2.0f, 2.0f, 2.0f)); // 缩放矩阵

// 视图和投影矩阵
Matrix4 view = Matrix4::LookAt(
    Vector3(0.0f, 5.0f, 5.0f),  // 眼睛位置
    Vector3(0.0f, 0.0f, 0.0f),  // 目标位置
    Vector3(0.0f, 1.0f, 0.0f)   // 上向量
);

Matrix4 perspective = Matrix4::Perspective(
    Math::DegToRad(60.0f),  // 视场角
    16.0f / 9.0f,          // 宽高比
    0.1f,                  // 近平面
    100.0f                 // 远平面
);

Matrix4 ortho = Matrix4::Orthographic(
    -10.0f, 10.0f,  // 左右
    -10.0f, 10.0f,  // 下上
    0.1f, 100.0f    // 近远
);

// 组合变换 (先缩放，再旋转，最后平移)
Matrix4 transform = t * r * s;

// 从矩阵提取变换
Vector3 translation = transform.GetTranslation();
Vector3 scale = transform.GetScale();
Quaternion rotation = transform.GetRotation();

// 矩阵分解
Vector3 decomposedScale;
Quaternion decomposedRotation;
Vector3 decomposedTranslation;
transform.Decompose(decomposedScale, decomposedRotation, decomposedTranslation);
```

## 四元数类

### Quaternion

四元数类，用于表示3D旋转，避免万向节锁问题。

#### 基本用法

```cpp
// 创建四元数
Quaternion a;                           // 单位四元数
Quaternion b = Quaternion::Identity();  // 单位四元数
Quaternion c(0.0f, 0.0f, 0.0f, 1.0f);   // 直接指定分量(x,y,z,w)

// 从其他表示创建
Quaternion fromEuler = Quaternion::FromEulerAngles(Math::PI/4, 0.0f, 0.0f); // 从欧拉角
Quaternion fromAxis = Quaternion(Vector3(0.0f, 1.0f, 0.0f), Math::PI/2); // 从轴角
Quaternion fromMatrix = Quaternion::FromRotationMatrix(Matrix3::Rotation(Math::PI/2)); // 从旋转矩阵

// 转换为其他表示
Vector3 eulerAngles = fromEuler.ToEulerAngles();  // 转为欧拉角
Matrix3 rotMat3 = fromEuler.ToMatrix3();          // 转为3x3旋转矩阵
Matrix4 rotMat4 = fromEuler.ToMatrix4();          // 转为4x4旋转矩阵

Vector3 axis;
float angle;
fromAxis.ToAxisAngle(axis, angle);  // 转为轴角表示

// 四元数操作
float length = c.Length();            // 长度
Quaternion normalized = c.Normalized(); // 归一化
Quaternion inverse = c.Inverse();      // 求逆
Quaternion conjugate = c.Conjugate();  // 共轭

// 旋转向量
Vector3 v(1.0f, 0.0f, 0.0f);
Vector3 rotated = fromEuler * v;  // 旋转向量

// 四元数组合 (先应用a再应用b)
Quaternion combined = b * a;

// 四元数插值
Quaternion slerped = Quaternion::Slerp(a, b, 0.5f);  // 球面插值
Quaternion lerped = Quaternion::Lerp(a, b, 0.5f);    // 线性插值(结果会被归一化)
```

## 数学常量和工具函数

Orange引擎数学库提供了常用的数学常量和工具函数。

```cpp
// 数学常量
float pi = Math::PI;          // π (3.14159...)
float halfPi = Math::HALF_PI; // π/2
float twoPi = Math::TWO_PI;   // 2π
float e = Math::E;            // e (2.71828...)

// 角度与弧度转换
float radians = Math::DegToRad(45.0f);  // 度数转弧度
float degrees = Math::RadToDeg(Math::PI / 4.0f); // 弧度转度数

// 通用数学函数
float clamped = Math::Clamp(value, 0.0f, 1.0f); // 将值限制在[0,1]范围内
float interpolated = Math::Lerp(a, b, 0.5f);    // 线性插值
```

## 最佳实践

- 使用 `const` 引用传递向量和矩阵参数，避免不必要的复制
- 优先使用四元数表示旋转而非欧拉角，以避免万向节锁问题
- 使用提供的静态方法创建特殊矩阵和向量，而不是手动构造
- 在性能关键的代码中，考虑复用变量而不是创建临时对象 