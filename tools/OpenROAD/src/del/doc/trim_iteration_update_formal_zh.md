# DEL 单轮 trim 坐标更新函数：完整形式化定义

本文针对当前 `src/del` 实现，单独说明连续迭代的一轮更新，不包含迭代结束后的 LP/MIP 后处理。
日期：2026-10-13。

从用户关心的接口出发：输入是当前所有 trim 的中心坐标 $(x,y)$，输出是下一轮所有 trim 的中心坐标 $(x',y')$。
但要与当前程序一致，还必须输入固定的原始坐标、约束图、移动域、当前轮号以及每条边的停滞计数。
下面先给出最终函数，再把其中每个算子展开，最后给出完整伪代码与数值例子。

## 阅读约定：上一轮、本轮、下一轮分别指什么

本文描述一次从第 $k$ 轮结束状态到第 $k+1$ 轮结束状态的更新：

- **上一轮位置／本轮输入**：$\mathbf p_i^{(k)}=(x_i^{(k)},y_i^{(k)})$。
- **本轮结束位置／下一轮输入**：$\mathbf p_i^{(k+1)}=(x_i^{(k+1)},y_i^{(k+1)})$。
- **初始位置**：$\mathbf p_i^{(0)}$，是本次运行固定基准，通常不等于上一轮位置。
- **本轮总移动**：$\Delta\mathbf p_i=\mathbf p_i^{(k+1)}-\mathbf p_i^{(k)}$，与相对初始点的累计位移不同。

粗体 $\mathbf p,\mathbf u,\mathbf c,\mathbf a$ 表示向量；不带粗体的 $u_i,c_e$ 是其中一个分量。帽子 $\widehat u_i$ 表示梯度阶段的中间结果，未必是最终值。下标 $i$ 标识 trim、$e$ 标识边；上标 $(k)$ 是轮次标签，不是乘方。撇号在 $\phi'$、$F'$ 中表示导数，在 $w_s'$、$w_t'$ 中表示跳跃候选新值，两种用法会在对应公式下区分。

文中每一组独立显示公式都附有当地的符号说明与计算含义；第 2 节总表用于快速查阅。若同一符号出现在多个公式，会在使用处再次说明。

## 1. 先给出“终极更新函数”

**记号说明：$P^{(k)}$ 的上标 $(k)$ 是轮次标签，不是乘方；表示完成第 $k$ 轮后的坐标集合。真正的乘方写作 $u_i^2$、$2^{-b}$ 等。$u_i^{(k)}$、$c_e^{(k)}$ 采用相同轮次约定。候选标签 $(A,b)$ 也不是乘方。**

对某一层，共有 $N$ 个 trim，当前已完成 $k$ 轮，令：

**公式 1：上一轮输入坐标与累计位移。**

$$
\begin{aligned}
P^{(k)} &= \{\mathbf p_i^{(k)}\}_{i=0}^{N-1}, &
\mathbf p_i^{(k)} &= (x_i^{(k)},y_i^{(k)}),\\
P^{(0)} &= \{\mathbf p_i^{(0)}\}_{i=0}^{N-1}, &
u_i^{(k)} &= \frac{\mathbf a\cdot(\mathbf p_i^{(k)}-\mathbf p_i^{(0)})}{W}.
\end{aligned}
$$

**本公式的符号说明：**

- $i$ 是某个 trim 的编号，$N$ 是该层 trim 总数；$i=0,\ldots,N-1$ 表示遍历这一层的全部 trim。
- $k$ 是已经完成的轮数。本次要执行第 $k+1$ 轮，所以 $P^{(k)}$ 是上一轮结束后、进入本轮时的坐标集合。
- $\mathbf p_i^{(k)}=(x_i^{(k)},y_i^{(k)})$ 是第 $i$ 个 trim 的上一轮中心点；$x_i^{(k)}$、$y_i^{(k)}$ 分别是它的横、纵坐标，单位 DBU。
- $P^{(0)}$、$\mathbf p_i^{(0)}$ 是本次运行开始时的初始坐标，作为固定基准；它们不会每轮重置成当前坐标。
- $\mathbf a$ 是允许移动的方向向量，$W$ 是该层线宽，单位 DBU；点积 $\mathbf a\cdot(\cdot)$ 提取沿允许方向的位移。
- $u_i^{(k)}$ 是上一轮结束时相对于初始位置的累计位移除以 $W$，没有单位；集合括号 $\{\cdot\}$ 表示把所有 trim 的坐标组成一组。

**本公式在做什么：** 这组公式把上一轮的实际坐标转换为求解器使用的一维变量。先减去固定初始点，得到累计位移；再取沿轨分量并除以线宽。例如水平层从初始 X=100 移到当前 X=103，若 W=10，则累计归一化位移为 0.3。这不是本轮准备移动的增量。


原始与当前中心均以 DBU 为单位，线宽为 $W>0$。沿轨方向为：

**公式 2：允许移动的方向。**

$$
\mathbf a=\begin{cases}(1,0),&\text{horizontal layer},\\(0,1),&\text{vertical layer}.\end{cases}
$$

**本公式的符号说明：**

- $\mathbf a=(a_x,a_y)$ 是沿轨方向单位向量；$a_x$ 和 $a_y$ 是它的 X、Y 分量。
- $(1,0)$ 表示只允许改变 X，$(0,1)$ 表示只允许改变 Y。
- 分段中的 horizontal layer、vertical layer 分别表示水平布线层和垂直布线层。

**本公式在做什么：** 方向向量决定一维位移如何还原成 XY 位移。水平层更新 X 而保留 Y；垂直层更新 Y 而保留 X。两种情况使用同一套更新公式。


核心函数定义为：

**公式 3：完整的一轮更新接口。**

$$
\begin{aligned}
(P^{(k+1)},\mathbf c^{(k+1)})
 &= \operatorname{UPDATE}(P^{(k)},\mathbf c^{(k)};\mathcal C,k),\\
\mathcal C
 &= \{P^{(0)},\mathbf a,W,G,\mathbf L,\mathbf U,\mathbf q,
       \mathcal I,\boldsymbol\theta,m,\eta,\tau,\alpha,\varepsilon\},\\
\widehat{\mathbf u}
 &= \operatorname{GRADIENT\_BACKTRACK}
       (\mathbf u^{(k)};\mathcal C,\lambda_{k+1}),\\
(\mathbf u^{(k+1)},\mathbf c^{(k+1)})
 &= \operatorname{STALL\_JUMP}
       (\widehat{\mathbf u},\mathbf u^{(k)},\mathbf c^{(k)};\mathcal C),\\
\mathbf p_i^{(k+1)}
 &= \mathbf p_i^{(0)}+W\mathbf a\,u_i^{(k+1)}.
\end{aligned}
$$

**本公式的符号说明：**

- $P^{(k)}$ 是上一轮结束后的全部 trim 坐标；$P^{(k+1)}$ 是本轮完成后的下一组坐标。
- $\mathbf c^{(k)}$、$\mathbf c^{(k+1)}$ 分别是本轮输入和输出的所有边停滞计数向量；它们是历史状态。
- $\operatorname{UPDATE}$ 是完整一轮更新函数；分号后的 $\mathcal C,k$ 是固定上下文和已完成轮数。
- $\mathcal C$ 中的 $P^{(0)}$ 是初始坐标，$\mathbf a$ 是移动方向，$W$ 是线宽，$G$ 是固定有序约束图。
- $\mathbf L,\mathbf U$ 是各节点累计归一化位移的上下界数组；$\mathbf q$ 是各节点的度数加一数组；$\mathcal I$ 收集每个节点的关联边编号。
- $\boldsymbol\theta=(\theta_A,\theta_B,\theta_C)$ 是规则阈值；$m$ 是梯度移动上限；$\eta$ 是跨峰值目标偏移；$\tau$ 是跳跃触发的残差门槛。
- $\alpha$ 是累计位移正则权重，$\varepsilon$ 是平滑尺度；$\lambda_{k+1}$ 是第 $k+1$ 轮规则能量的权重。
- $\mathbf u^{(k)}$ 是上一轮的累计归一化位移；$\widehat{\mathbf u}$ 是完成梯度与回溯后的中间位移；$\mathbf u^{(k+1)}$ 是完成跳跃后的最终位移。
- $\operatorname{GRADIENT\_BACKTRACK}$ 表示梯度与回溯阶段；$\operatorname{STALL\_JUMP}$ 表示停滞计数与有序跳跃阶段。
- $\mathbf p_i^{(0)}$ 是第 $i$ 个 trim 的初始中心；$W\mathbf a\,u_i^{(k+1)}$ 把最终累计位移转换为 DBU 二维向量。

**本公式在做什么：** 先将上一轮位置送入梯度阶段，得到中间结果；再结合旧位置和停滞历史执行跳跃，得到下一轮状态；最后从固定初始中心重建新 XY 坐标。只知道当前坐标不足以决定是否跳跃，停滞计数也必须传入。


为了直接看清本轮 $(x,y)$ 到 $(x',y')$ 的关系，定义：

**公式 4：拆分本轮增量的来源。**

$$
\begin{aligned}
r_i &= u_i^{(k+1)}-\widehat u_i,\\
\delta_i &= (\widehat u_i-u_i^{(k)})+r_i.
\end{aligned}
$$

**本公式的符号说明：**

- $u_i^{(k)}$ 是本轮开始时的累计归一化位移；$\widehat u_i$ 是梯度阶段结束时的累计归一化位移。
- $u_i^{(k+1)}$ 是跳跃也结束后的最终累计归一化位移。
- $r_i$ 是跳跃阶段新增的归一化修正，等于最终值减中间值。
- $\delta_i$ 是本轮总归一化增量，包含梯度增量和跳跃修正；$i$ 指同一个 trim，$k$ 指上一轮。

**本公式在做什么：** 将本轮移动分成两部分，便于追踪来源。两部分相加等于最终累计位移减去旧累计位移。没有成功跳跃时修正为零；梯度没有接受候选时，梯度增量为零。


**公式 5：上一轮 XY 点到下一轮 XY 点。**

$$
\boxed{
\begin{aligned}
x_i^{(k+1)} &= x_i^{(k)}+Wa_x\delta_i,\\
y_i^{(k+1)} &= y_i^{(k)}+Wa_y\delta_i.
\end{aligned}
}
$$

**本公式的符号说明：**

- $(x_i^{(k)},y_i^{(k)})$ 是第 $i$ 个 trim 在上一轮结束后的中心点，也就是本轮输入点，单位 DBU。
- $(x_i^{(k+1)},y_i^{(k+1)})$ 是该 trim 在本轮结束后的中心点，也就是下一轮将使用的点。
- $W$ 是层线宽，$\delta_i$ 是本轮总归一化增量；$W\delta_i$ 是本轮沿轨实际移动量，单位 DBU。
- $a_x,a_y$ 是允许方向 $\mathbf a$ 的两个分量；水平层为 1、0，垂直层为 0、1。
- $k$ 与 $k+1$ 表示相邻轮次，$i$ 在等式两边始终指同一个 trim。

**本公式在做什么：** 下一轮中心等于上一轮中心加本轮平移量。水平层右移时增量为正、左移时为负，Y 不变；垂直层上移时增量为正、下移时为负，X 不变。该公式使用本轮增量，不能将累计位移再次加到上一轮中心，否则会重复累计。


当线搜索接受某个候选 $(A^\ast,b^\ast)$ 时，梯度部分进一步展开为：

**公式 6：回溯接受后的一维更新。**

$$
\begin{aligned}
\widehat u_i
 &= \operatorname{clip}_{[L_i,U_i]}\!\left(
       u_i^{(k)}
       +\operatorname{clip}_{[-m,m]}
          \!\left(S\,2^{-b^\ast}(v_i+j_i^{(A^\ast)})\right)
    \right),\\
g_i
 &= \frac{\alpha u_i^{(k)}}{\sqrt{(u_i^{(k)})^2+\varepsilon^2}}
       +\lambda_{k+1}\sum_{e=0}^{E-1}
          B_{ei}\phi'_e(d_e(\mathbf u^{(k)})),\\
v_i
 &= \operatorname{clip}_{[L_i-u_i^{(k)},\,U_i-u_i^{(k)}]}
       \!\left(-\frac{g_i}{q_i}\right),\\
M &= \max_i |v_i|,\qquad
S = \begin{cases}m/M,&M>m,\\1,&M\le m.\end{cases}
\end{aligned}
$$

**本公式的符号说明：**

- $u_i^{(k)}$ 是上一轮累计归一化位移，$\widehat u_i$ 是本轮梯度阶段接受的累计归一化位移。
- $L_i,U_i$ 是节点允许的累计位移范围；$\operatorname{clip}_{[\ell,h]}$ 表示把输入截断到闭区间内。
- $m$ 是单节点梯度位移上限，$S$ 是整层方向缩放系数；内层 clip 限制增量，外层 clip 限制最终位置。
- $A^\ast$ 是第一个通过接受条件的试算种类，$b^\ast$ 是它的回溯次数；$2^{-b^\ast}$ 表示每回溯一次将步长减半，不是轮次标签。
- $v_i$ 是节点下降方向，$j_i^{(A^\ast)}$ 是该试算的扰动；$M$ 是整层最大方向绝对值。
- $g_i$ 是目标对该节点归一化位移的梯度；$q_i$ 是节点关联边数加一，用于减小高连接节点的移动。
- $\alpha$、$\varepsilon$ 是正则权重和平滑尺度；$\lambda_{k+1}$ 是本轮规则权重。
- $E$ 是约束边总数，$e$ 是边编号，$\sum_{e=0}^{E-1}$ 是对全部边求和；$B_{ei}$ 在节点是 src 时为 -1、是 dst 时为 +1、否则为 0。
- $d_e(\mathbf u^{(k)})$ 是上一轮该边的归一化距离；$\phi'_e$ 是该边规则势能对距离的导数；$\mathbf u^{(k)}$ 收集整层旧位移。
- $\max_i|v_i|$ 取所有节点方向幅度的最大值；平方与平方根用于平滑正则项，均为通常数学运算。

**本公式在做什么：** 先由旧位置算梯度，取负梯度并除以连接数，限制到可移动方向，再按整层最大方向缩放。回溯选择试算种类和减半次数后，才能确定最终接受的中间位置。必须保持先限制方向、后缩放、再加入扰动的顺序。


如果全部候选失败，则 $\widehat{\mathbf u}=\mathbf u^{(k)}$。如果本轮没有成功跳跃，则 $\mathbf r=\mathbf0$。
所以完整更新包含三种可能：仅梯度移动、梯度加跳跃、梯度不动但跳跃移动；也可能都不动。

作为对照，下式中的 $\gamma$ 是假设的固定学习率，$\nabla_{\mathbf p_i}E$ 是对二维中心的能量梯度；它没有表达回溯与历史跳跃。

**这就是原代码的一轮，而不是简单的 $\mathbf p_i^{(k+1)}=\mathbf p_i^{(k)}-\gamma\nabla_{\mathbf p_i}E$。**
$A^\ast,b^\ast$ 由整层能量的回溯判定得到，$\mathbf r$ 由停滞状态和按序跳跃得到；二者都不是预先固定的常数。

完整实现入口：[Optimizer.cpp:212](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:212)。

## 2. 坐标、静态参数与动态状态：逐个变量解释

### 2.1 坐标约定

$(x_i^{(k)},y_i^{(k)})$ 表示 trim **中心**的连续坐标，不是左下角，也不是 GUI 上取整后的显示坐标。
如果输入是矩形 $(x_{\min},y_{\min},x_{\max},y_{\max})$，先换算：

**公式 7：从矩形边界得到中心。**

$$
x=\frac{x_{\min}+x_{\max}}{2},
\qquad
y=\frac{y_{\min}+y_{\max}}{2}.
$$

**本公式的符号说明：**

- $x_{\min},x_{\max}$ 是 trim 左、右边界坐标；$y_{\min},y_{\max}$ 是下、上边界坐标，单位 DBU。
- $x,y$ 是此矩形中心的 X、Y 坐标；除以 2 表示取两个相对边界的平均值。

**本公式在做什么：** 如果手中输入是 trim 盒边界，先转成中心坐标再应用更新函数。中心允许有 0.5 DBU，不应为了显示方便提前取整。


trim 的尺寸不变，更新后矩形四个边界同时平移 $(\Delta x,\Delta y)$。
例如水平层的矩形更新为 $(x_{\min}+\Delta x,y_{\min},x_{\max}+\Delta x,y_{\max})$。

横轨坐标始终保持原值。因此可接受的当前状态必须满足：

**公式 8：固定原点重建当前位置。**

$$
\mathbf p_i^{(k)}=\mathbf p_i^{(0)}+W\mathbf a\,u_i^{(k)}.
$$

**本公式的符号说明：**

- $\mathbf p_i^{(0)}$ 是本次运行初始中心；$\mathbf p_i^{(k)}$ 是上一轮结束后的当前中心。
- $u_i^{(k)}$ 是相对初始点的累计归一化位移；$W$ 将它恢复为 DBU。
- $\mathbf a$ 是移动方向单位向量；$i$ 表示 trim，$(0)$ 和 $(k)$ 是初始与当前轮次标签。

**本公式在做什么：** 当前中心必须能写成初始中心加沿轨平移。它既解释求解器坐标的含义，也说明横轨分量必须保持初始值。不能在每轮更新后把当前中心当作新的正则原点。


水平层要求 $y_i^{(k)}=y_i^{(0)}$；垂直层要求 $x_i^{(k)}=x_i^{(0)}$。
如果任意改变横轨坐标，就不再对应这份固定图上的当前求解器状态。

源码中的 `center_x2、center_y2` 是原始中心的双倍值；半 DBU 中心不会在求解时被截断。
本文统一用 $u$ 表示归一化位移，避免把源码 `positions_` 中的一维变量 x 与用户的二维 X 坐标混淆。
初始化对应：[Optimizer.cpp:68](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:68)。

### 2.2 变量总表

| 符号 | 含义 | 单位／范围 | 源码对应 |
|---|---|---|---|
| $i$ | 当前层节点编号 | $0,\ldots,N-1$ | `layer.nodes[i]` |
| $e$ | 当前层有序边编号 | $0,\ldots,E-1$ | `layer.arcs[e]` |
| $k$ | 已完成的更新轮数 | 初始为 0 | 进入本次 step 时 `state.iteration=k+1` |
| $P^{(0)}$ | 本次 run 的原始 trim 中心 | DBU | `Vnode.center_x2/2,center_y2/2` |
| $P^{(k)}$ | 当前连续 trim 中心 | DBU | 由原始中心与 `positions_` 重建 |
| $\mathbf a$ | 层的首选方向 | $(1,0)$ 或 $(0,1)$ | `layer.horizontal` |
| $W$ | 层线宽 | 正 DBU | `width_` |
| $\mathbf u^{(k)}$ | 相对原点的累计位移除以 W | 无量纲 | `positions_` |
| $L_i,U_i$ | 节点累计归一化位移下／上界 | 无量纲 | `low_[i],up_[i]` |
| $G$ | 固定有序约束图 | src、dst、type、合法区间 | `layer_` 的节点与边 |
| $s_e,t_e$ | 边的起点／终点编号 | 节点编号 | `arc.src,arc.dst` |
| $B_{ei}$ | 有向关联系数：src=-1，dst=+1，其余=0 | 无量纲 | 边梯度的减／加操作 |
| $q_i$ | 1 加关联边数 | 正整数 | `degree_[i]` |
| $\mathcal I_i$ | 节点的关联边编号集合 | 边编号 | `incident_arcs_[i]` |
| $d_e$ | 边当前沿轨有符号中心差除以 W | 无量纲 | `arcDistance()` |
| $\theta_A,\theta_B,\theta_C$ | 三类规则阈值除以 W | 当前为 5、5、3 | `rules.d/dt/du / width_` |
| $\phi_e,\phi'_e$ | 边规则势能与对距离的导数 | 无量纲 | `rulePotential()` |
| $\alpha$ | 伸长正则项权重 | 0.01 | `kAlpha` |
| $\varepsilon$ | 平滑绝对值的尺度 | 0.01，相当于 0.01W 位移 | `kEpsilon` |
| $\lambda_{k+1}$ | 即将执行这一轮的规则权重 | 10／20／40／80 | `lambda` |
| $g_i$ | 能量对 $u_i$ 的梯度 | 对归一化变量求导 | `gradient[i]` |
| $v_i$ | 除度数并限制域后的下降方向 | 无量纲 | `direction[i]` |
| $M$ | 整层下降方向最大绝对分量 | 无量纲 | `largest` |
| $m$ | 梯度阶段每节点移动上限 | 无量纲，配置单位 W | `max_step_` |
| $S$ | 整层方向缩放系数 | $(0,1]$ | `scale` |
| $A$ | 试算种类：正常／两种扰动 | 0、1、2 | `attempt` |
| $b$ | 当前 attempt 的回溯编号 | 0…19 | `backtrack` |
| $j_i^{(A)}$ | 确定性归一化扰动 | 0 或 ±0.01 | `jitter` |
| $h_{A,b}$ | 当前试算的步长系数 | $S\,2^{-b}$ | `step_size` |
| $\mathbf z^{(A,b)}$ | 当前试算的累计归一化位移 | 各分量在域内 | `candidate` |
| $\widehat{\mathbf u}$ | 梯度阶段完成后的累计归一化位移 | 无量纲 | `gradientStep()` 后的 `positions_` |
| $c_e^{(k)}$ | 边的停滞历史计数 | 0…3 | `stalled_rounds_[e]` |
| $\tau$ | 停滞修复使用的最小违规残差 | DBU | `repair_tolerance_dbu_` |
| $\eta$ | 峰值另一侧的目标偏移除以 W | 正、无量纲 | `jump_offset_` |
| $r_i$ | 跳跃阶段对 û_i 的总修正 | 无量纲 | 由修复前后 `positions_` 相减 |
| $\delta_i$ | 本轮最终归一化增量 | 无量纲 | 最终位置减 `previous[i]` |
| $\Delta\mathbf p_i$ | 本轮二维中心增量 | DBU | $W\mathbf a\,\delta_i$ |

其中：

**公式 9：域边界与单位转换。**

$$
\begin{aligned}
L_i&=\frac{\ell_i^{[2]}-s_i^{[2]}}{2W},&
U_i&=\frac{h_i^{[2]}-s_i^{[2]}}{2W},\\
\eta&=\frac{J_{\mathrm{nm}}D_{\mathrm{DBU}/\mu\mathrm m}}{1000W},&
\tau&=\frac{5D_{\mathrm{DBU}/\mu\mathrm m}}{1000}.
\end{aligned}
$$

**本公式的符号说明：**

- $L_i,U_i$ 是节点累计归一化位移下、上界；$i$ 是节点编号，$W$ 是线宽。
- $\ell_i^{[2]},h_i^{[2]}$ 是沿轨中心绝对下、上界的双倍值，$s_i^{[2]}$ 是初始沿轨中心的双倍值；上标 $[2]$ 表示双倍坐标，不是轮次或平方。
- 减去 $s_i^{[2]}$ 得到相对初始中心的双倍位移，除以 $2W$ 得到归一化上下界。
- $J_{\mathrm{nm}}$ 是用户配置的 jump_offset_nm，单位 nm；$D_{\mathrm{DBU}/\mu\mathrm m}$ 是每微米对应多少 DBU。
- $\eta$ 是跳跃目标相对峰值的归一化偏移；$\tau$ 是正常 Del 调用使用的修复残差门槛，单位 DBU。
- 常数 1000 将 nm 转为 μm；常数 5 对应正常流程的 5nm 门槛。

**本公式在做什么：** 移动域原本是绝对中心边界，要转成相对初始位置的变量范围。跳跃偏移也需要从 nm 转到与位移变量一致的归一化单位。blocked 节点直接固定上下界为零。


这里上标 $[2]$ 表示双倍坐标，区别于轮次标签 $(k)$。其中 $\ell_i^{[2]}$ 对应 node.low2，$h_i^{[2]}$ 对应 node.up2，$s_i^{[2]}$ 对应 along2(node)；blocked 节点取 $L_i=U_i=0$。$J_{\mathrm{nm}}$ 是 jump_offset_nm，$D_{\mathrm{DBU}/\mu\mathrm m}$ 是 dbu_per_micron；$\tau$ 的上式用于正常 Del 调用。

`LayerOptimizer` 可直接传其他 τ；本文的 UPDATE 将其列为参数，不把 5nm 写死。
首次初始化 $\mathbf u^{(0)}=\mathbf0,\ \mathbf c^{(0)}=\mathbf0$，因此 $P^{(0)}$ 也是初始输入坐标。

**只有单个 trim 的 $(x_i,y_i)$ 无法确定它的更新量**：梯度需要关联节点的位置，
线搜索接受条件需要整层能量，跳跃还需要边历史与处理顺序。
即使给出所有当前坐标，缺少 $P^{(0)}$ 也无法算累计位移正则项；缺少 $\mathbf c^{(k)}$ 则无法决定是否跳跃。

## 3. 从 $(x,y)$ 到梯度：完整目标函数

### 3.1 约束距离

**公式 10：当前两端坐标如何得到边距离。**

$$
\begin{aligned}
d_e(\mathbf u)
 &= \frac{\mathbf a\cdot(\mathbf p_{t_e}^{(0)}-\mathbf p_{s_e}^{(0)})}{W}
       +u_{t_e}-u_{s_e}\\
 &= \frac{\mathbf a\cdot(\mathbf p_{t_e}-\mathbf p_{s_e})}{W}.
\end{aligned}
$$

**本公式的符号说明：**

- $e$ 是边编号，$s_e,t_e$ 是其 src、dst 节点编号；不带 e 的下标 s、t 在后文是这两个编号的简写。
- $\mathbf p_{s_e}^{(0)},\mathbf p_{t_e}^{(0)}$ 是两端初始中心；$\mathbf p_{s_e},\mathbf p_{t_e}$ 是传入状态中的当前中心。
- $u_{s_e},u_{t_e}$ 是两端当前累计归一化位移；$\mathbf u$ 是整层位移数组。
- $\mathbf a$ 是允许方向，$W$ 是线宽，$d_e(\mathbf u)$ 是有符号沿轨中心差除以线宽。
- 点积只保留沿轨分量；减法顺序为 dst 减 src，因此距离可以为负。

**本公式在做什么：** 当前距离等于原始中心差加两端相对移动之差。dst 沿正方向移动会增加距离，src 沿正方向移动会减小距离，这也决定后面梯度累加的正负号。


水平层为 $(x_{t_e}-x_{s_e})/W$；垂直层为 $(y_{t_e}-y_{s_e})/W$。
不是中心间的二维直线距离，也不是矩形边缘间距。
源码：[Optimizer.cpp:203](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:203)。

### 3.2 规则势函数与导数

定义下界罚函数：

**公式 11：不足下界时的二次罚函数。**

$$
\operatorname{LB}(d,h)=\bigl[\min(0,d-h)\bigr]^2,
\qquad
\operatorname{LB}'(d,h)=2\min(0,d-h).
$$

**本公式的符号说明：**

- $d$ 是当前归一化有符号距离，$h$ 是该罚函数要求的归一化下界；这里 h 不是回溯步长 $h_{A,b}$。
- $\operatorname{LB}(d,h)$ 是下界违规的势能；$\operatorname{LB}'(d,h)$ 是它对 d 的导数，撇号表示求导。
- $\min(0,d-h)$ 在距离满足下界时取零，不满足时取负差值；外面的平方是真正的二次方。

**本公式在做什么：** 距离足够时没有罚能；距离不足时，短缺量越大，罚能按平方增加。导数为负意味着沿增加距离的方向能够降低该罚能。


定义区间势垒：

**公式 12：区间势垒的能量。**

$$
t=\frac{d-a_0}{b_0-a_0},
\qquad
F(d;a_0,b_0)=
\begin{cases}
16t^2(1-t)^2,&a_0<d<b_0,\\
0,&\text{otherwise},
\end{cases}
$$

**本公式的符号说明：**

- $d$ 是当前归一化距离，$a_0,b_0$ 是势垒区间左、右端点，要求 $b_0>a_0$；它们不是方向向量 $\mathbf a$。
- $t$ 是 d 在区间内的相对比例，从左端的 0 变化到右端的 1；它不是 dst 节点编号 $t_e$。
- $F(d;a_0,b_0)$ 是这一个区间的势垒能量；分号区分距离自变量与区间参数。
- 常数 16 将中点势能规范到 1；$t^2$ 和 $(1-t)^2$ 是普通平方；otherwise 表示不在开区间内。

**本公式在做什么：** 能量在两个端点为零，在中点最高。区间内的点可以朝任一合法端点下降，所以它不是单调推远的罚函数；这产生合并与分离两种趋势。


**公式 13：区间势垒的导数。**

$$
F'(d;a_0,b_0)=
\begin{cases}
\dfrac{32t(1-t)(1-2t)}{b_0-a_0},&a_0<d<b_0,\\
0,&\text{otherwise}.
\end{cases}
$$

**本公式的符号说明：**

- $F'$ 是势垒 F 对当前距离 d 的导数，撇号表示求导。
- $a_0,b_0$ 是区间端点，$t=(d-a_0)/(b_0-a_0)$ 是区间比例；$b_0-a_0$ 是区间宽度。
- $1-2t$ 决定导数符号；常数 32 来自上一条势垒公式的求导。
- 分段条件 $a_0<d<b_0$ 表示势垒内部，其他位置返回零。

**本公式在做什么：** 左半区导数为正，减小距离可降低能量；右半区导数为负，增大距离可降低能量；中点导数为零，即使仍违规也没有规则梯度，因此需要扰动和停滞跳跃。


$a_0,b_0$ 是区间端点，和方向向量 a 是不同变量。

**公式 14：A、B、C 约束分别使用哪一种势能。**

$$
\begin{aligned}
\phi_A(d)&=\operatorname{LB}(d,\theta_A),\\
\phi_B(d)&=
\begin{cases}
\operatorname{LB}(d,0),&d<0,\\
F(d;0,\theta_B),&d\ge0,
\end{cases}\\
\phi_C(d)&=
\begin{cases}
F(d;-\theta_C,0),&d<0,\\
F(d;0,\theta_C),&d\ge0.
\end{cases}
\end{aligned}
$$

**本公式的符号说明：**

- $\phi_A,\phi_B,\phi_C$ 分别是 A、B、C 类型边的规则势能；这里 A/B/C 是边类型，不是试算编号 A。
- $d$ 是当前归一化距离；$\theta_A,\theta_B,\theta_C$ 是三类阈值，当前分别为 5、5、3。
- $\operatorname{LB}$ 是下界二次罚函数，F 是区间势垒；它们的端点参数也使用归一化距离单位。
- $d<0$ 和 $d\ge0$ 决定 B/C 的分支；负号 $-\theta_C$ 是 C 的负距离合法阈值。

**本公式在做什么：** A 始终要求保持最小正间隔。B 允许合并到零或保持正间隔，同时处罚两端发生穿越。C 可以在负侧分离、对齐到零或在正侧分离，所以正负两侧都使用势垒。


导数按相同分支选择 $\operatorname{LB}'$ 或 $F'$。区间中点 $F'=0$，端点返回零。
源码：[Optimizer.cpp:15](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:15)。

### 3.3 总能量与梯度

**公式 15：整层总能量与每个 trim 的梯度。**

$$
\begin{aligned}
E_\lambda(\mathbf u)
 &= \alpha\sum_{i=0}^{N-1}\left(\sqrt{u_i^2+\varepsilon^2}-\varepsilon\right)
       +\lambda\sum_{e=0}^{E-1}\phi_e(d_e(\mathbf u)),\\
g_i
 &= \frac{\partial E_\lambda}{\partial u_i}
  = \frac{\alpha u_i}{\sqrt{u_i^2+\varepsilon^2}}
       +\lambda\sum_{e=0}^{E-1}B_{ei}\phi'_e(d_e(\mathbf u)).
\end{aligned}
$$

**本公式的符号说明：**

- $E_\lambda(\mathbf u)$ 是传入整层位移状态的总能量；此处函数 E 与边数 E 是不同用法。
- $\mathbf u=(u_0,\ldots,u_{N-1})$ 是各 trim 的累计归一化位移；N 是节点总数，i 是节点编号，e 是边编号，求和上界 E-1 使用的是边总数。
- $\alpha=0.01$ 是累计位移惩罚权重，$\varepsilon=0.01$ 是平滑尺度；$\lambda$ 是本轮规则权重。
- $\phi_e$ 是边规则势能，$d_e(\mathbf u)$ 是该边当前距离，$\phi'_e$ 是对距离的导数。
- $g_i$ 是能量对第 i 个节点位移的偏导；$\partial$ 表示只改变该变量，其他节点暂时不变。
- $B_{ei}$ 是边到节点的正负关系：src 为 -1、dst 为 +1、不相连为 0；两个 $\sum$ 分别累加全部节点和全部边。

**本公式在做什么：** 总目标一边偏好减少相对初始位置的移动，一边偏好降低规则违规势能。一个 trim 的梯度包括自身正则项和所有关联边的贡献，因此不能仅按一条冲突边决定它的更新。


用二维物理坐标直接写同一个目标：

**公式 16：用实际 XY 坐标表达同一个能量。**

$$
\begin{aligned}
E_\lambda(P)
 &= \alpha\sum_i\left[
       \sqrt{\left(\frac{\mathbf a\cdot(\mathbf p_i-\mathbf p_i^{(0)})}{W}\right)^2
                  +\varepsilon^2}
       -\varepsilon\right]\\
 &\quad+\lambda\sum_e\phi_e\!\left(
       \frac{\mathbf a\cdot(\mathbf p_{t_e}-\mathbf p_{s_e})}{W}\right),\\
\frac{\partial E}{\partial x_i}
 &=\frac{a_x}{W}g_i,\qquad
\frac{\partial E}{\partial y_i}
 =\frac{a_y}{W}g_i.
\end{aligned}
$$

**本公式的符号说明：**

- P 是整层当前中心坐标集合，$\mathbf p_i=(x_i,y_i)$ 是当前中心，$\mathbf p_i^{(0)}$ 是固定初始中心。
- $\mathbf a=(a_x,a_y)$ 是允许方向，W 是线宽；点积除以 W 把实际中心差换算成归一化位移或距离。
- $\alpha,\varepsilon,\lambda$ 分别是正则权重、平滑尺度、本轮规则权重；i、e 分别遍历节点和边。
- $s_e,t_e$ 是边两端编号，$\phi_e$ 是该边势函数；$g_i$ 是对归一化位移而非物理 XY 求得的梯度。
- $\partial E/\partial x_i$、$\partial E/\partial y_i$ 是对实际中心 X、Y 的偏导；$a_x/W,a_y/W$ 是链式求导的单位换算因子。

**本公式在做什么：** 同一个目标可以用一维位移或 XY 中心描述，但梯度的尺度不同。水平层 Y 偏导为零、垂直层 X 偏导为零。若直接拿 XY 梯度代替源码归一化梯度，必须补上 W 的换算。


源码先对归一化变量 u 求梯度，再乘 W 重建坐标。
若实现直接使用 XY 梯度，要注意这个 $1/W$ 因子：忽略单位转换会改变步长。
不考虑 clamp、jitter、全层缩放时，源码的物理方向是：

**公式 17：归一化方向与物理坐标方向的关系。**

$$
\Delta s_i^{\mathrm{direction}}
 =-\frac{Wg_i}{q_i}
 =-\frac{W^2}{q_i}\frac{\partial E}{\partial s_i},
\qquad s_i=\mathbf a\cdot\mathbf p_i.
$$

**本公式的符号说明：**

- $s_i=\mathbf a\cdot\mathbf p_i$ 是第 i 个 trim 的当前沿轨绝对中心坐标，单位 DBU；它不是 src 编号 $s_e$。
- $\Delta s_i^{\mathrm{direction}}$ 是尚未执行 clip、整层缩放和回溯的物理下降方向；上标 direction 是标签。
- $g_i$ 是对归一化位移的梯度，W 是线宽，$q_i$ 是关联边数加一。
- $\partial E/\partial s_i$ 是能量对物理沿轨坐标的偏导；$W^2$ 是普通平方。

**本公式在做什么：** 源码先在归一化空间下降，再乘 W 恢复实际移动，所以等价物理方向含 W 的平方。此式用于理解单位，不能独立替代完整更新，因为真正移动还要受域、步长和能量接受条件限制。


源码：[Optimizer.cpp:89](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:89)。

### 3.4 轮号约定

本文用 $k$ 表示已经完成的轮数，正在求第 $k+1$ 轮：

**公式 18：本轮规则权重的调度。**

$$
\lambda_{k+1}=10\cdot2^{\min\{3,\lfloor k/25\rfloor\}}.
$$

**本公式的符号说明：**

- k 是已完成的轮数，$\lambda_{k+1}$ 是即将执行的第 k+1 轮权重。
- $\lfloor k/25\rfloor$ 表示先除以 25 再向下取整，每完成 25 轮进入下一档。
- $\min\{3,\cdot\}$ 将档位最多限制为 3；$2^{(\cdot)}$ 是普通指数幂。
- 常数 10 是起始权重，25 是每档轮数，3 对应最后一档 80。

**本公式在做什么：** 前 25 次更新用 10，随后两档用 20 和 40，第 76 次起用 80。权重根据轮号确定，不根据本轮冲突数量或梯度大小自适应。


所以 $P^{(0)}\to P^{(1)}$ 用 $\lambda=10$；$P^{(25)}\to P^{(26)}$ 用 $\lambda=20$。
源码在更新前先递增 iteration，写成 `(iteration-1)/25`；二者相同。
第 0 轮的 evaluate 不调用 UPDATE，不产生移动。
源码：[RunOptimization.cpp:240](/workspace/openroad-26/src/del/src/optimize/RunOptimization.cpp:240)。

## 4. GRADIENT_BACKTRACK 算子：候选怎样成为真正更新

定义 $\operatorname{clip}_{[\ell,h]}(z)=\min(h,\max(\ell,z))$：$z$ 是待限制数值，$\ell,h$ 是下、上界，min/max 分别取较小／较大值；该算子把超界输入截回区间端点。逐节点计算：

**公式 19：所有回溯候选的生成规则。**

$$
\begin{aligned}
v_i&=\operatorname{clip}_{[L_i-u_i,U_i-u_i]}\!\left(-\frac{g_i}{q_i}\right),\\
M&=\max_i|v_i|,\qquad
S=\begin{cases}m/M,&M>m,\\1,&M\le m,\end{cases}\\
j_i^{(A)}&=
\begin{cases}
0,&A=0,\\
+0.01,&A\in\{1,2\},\ (i+A)\bmod2=0,\\
-0.01,&A\in\{1,2\},\ (i+A)\bmod2=1,
\end{cases}\\
h_{A,b}&=S\,2^{-b},\\
z_i^{(A,b)}
 &=\operatorname{clip}_{[L_i,U_i]}\!\left(
     u_i+\operatorname{clip}_{[-m,m]}\!\left(
       h_{A,b}(v_i+j_i^{(A)})\right)\right).
\end{aligned}
$$

**本公式的符号说明：**

- $u_i$ 是本轮开始时的累计归一化位移，$L_i,U_i$ 是固定移动域，$g_i$ 是在旧状态计算的梯度。
- $q_i$ 是关联边数加一；$v_i$ 是处理后的下降方向；clip 表示区间截断，max 与绝对值取整层最大方向幅度。
- M 是整层最大幅度，m 是梯度阶段单节点增量上限，S 是缩放系数。
- A 是试算编号：0 为无扰动，1、2 为两种相反的交替扰动；i 是原节点编号。
- $j_i^{(A)}$ 是扰动，mod 2 判断奇偶；±0.01 是归一化扰动幅度。
- b 是当前 A 的回溯次数，$h_{A,b}$ 是步长系数；$2^{-b}$ 每增加一次 b 都将步长减半。
- $z_i^{(A,b)}$ 是第 A 种试算、第 b 次回溯得到的候选累计位移；上标 (A,b) 是候选标签，不是乘方。

**本公式在做什么：** 每次候选都从本轮旧位置重新生成，不是接着上一次失败候选继续移动。正常方向失败才尝试扰动；同一轮梯度和方向不因回溯重新计算，只有步长减半。


注意三个细节：$S$ 在加入 jitter 前计算；同一 attempt 的所有候选都从旧 $\mathbf u$ 出发；
不是每次失败后在上一个 candidate 上继续更新。

按以下顺序搜索：$(A,b)=(0,0),\ldots,(0,19),(1,0),\ldots,(1,19),(2,0),\ldots,(2,19)$，但存在提前跳出：

1. 当前候选没有任何节点改变超过 $10^{-12}$，直接结束当前 A 的 b 循环，不计算其能量。
2. 对其余候选，接受条件为：

**公式 20：整层候选的接受条件。**

$$
E_\lambda(\mathbf z^{(A,b)})
 < E_\lambda(\mathbf u)-10^{-12}\max\{1,E_\lambda(\mathbf u)\}.
$$

**本公式的符号说明：**

- $\mathbf u$ 是本轮旧位移状态，$\mathbf z^{(A,b)}$ 是当前整层候选状态，A、b 是试算与回溯编号。
- $E_\lambda$ 是使用同一个本轮权重 λ 计算的总能量；左边为候选能量，右边第一项为旧能量。
- $10^{-12}$ 是数值下降门槛，$\max\{1,E_\lambda(\mathbf u)\}$ 根据旧能量尺度调整门槛。
- 严格小于号表示必须有足够的能量下降，等于或只有极小下降都不接受。

**本公式在做什么：** 只有整层总目标下降，才同时接受全部 trim 的候选位置。这样一条边的改善不能掩盖其他边造成的更大损失。本条件仅约束梯度阶段，不约束后面的势垒跳跃。


3. 接受第一个通过的候选后立刻返回 $\widehat{\mathbf u}$。
4. 当前 A 结束后，若旧点的 $\sum_e\phi_e=0$，不再尝试后面的扰动 A。
5. 所有可执行候选均失败，返回 $\widehat{\mathbf u}=\mathbf u$。

候选接受是整层判断。某个 trim 的局部边能量下降，并不足以单独接受该节点的位置。
在合法初始状态下，梯度阶段移动量满足 $|\widehat u_i-u_i|\le m$，即物理位移不超过 $mW$。
源码：[Optimizer.cpp:150](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:150)。

## 5. STALL_JUMP 算子：历史如何进入最终更新

### 5.1 先更新所有边的停滞计数

令 $K_e$ 为边的合法归一化距离集合：

**公式 21：合法集合、真实残差与修复资格。**

$$
\begin{aligned}
K_A&=[\theta_A,+\infty),\\
K_B&=\{0\}\cup[\theta_B,+\infty),\\
K_C&=(-\infty,-\theta_C]\cup\{0\}\cup[\theta_C,+\infty),\\
\rho_e(\mathbf u)
 &=\min_{[\ell,h]\in\mathcal R_e}
       \max\{0,\ell-d_e(\mathbf u),d_e(\mathbf u)-h\},\\
V_e(\mathbf u)
 &=\mathbf1\{\rho_e(\mathbf u)>0\ \land\ W\rho_e(\mathbf u)\ge\tau\}.
\end{aligned}
$$

**本公式的符号说明：**

- $K_A,K_B,K_C$ 是三种边的合法归一化距离集合，$\theta_A,\theta_B,\theta_C$ 是对应阈值。
- 区间方括号表示包含端点，圆括号表示不包含；$\{0\}$ 表示仅允许精确对齐的一个点；$\cup$ 是集合并集；±∞ 表示该侧没有有限边界。
- e 是边编号，$\mathcal R_e$ 是该边合法区间列表，$[\ell,h]$ 表示其中一个区间的上下界。
- $\mathbf u$ 是待判断的位移状态，$d_e(\mathbf u)$ 是当前边距离；$\rho_e(\mathbf u)$ 是距最近合法区间的违规残差。
- $\max$ 计算落在一个区间之外的偏离，外层 $\min$ 选择最近的合法区间。
- $V_e$ 是是否达到修复门槛的标志；$\mathbf1\{\cdot\}$ 是指示函数，$\land$ 表示条件同时成立。
- W 把残差从归一化单位转成 DBU，τ 是 DBU 残差门槛；必须同时满足正残差和不小于门槛。

**本公式在做什么：** 势能用于优化，残差用于判断几何是否合法，二者不是同一个量。B/C 的精确对齐允许为零距离，不能把靠近零却仍有正残差直接视为合法；小于门槛只意味着不触发这项修复。


$\mathcal R_e$ 是边 $e$ 的合法区间列表，其并集为 $K_e$；$\mathbf1\{\cdot\}$ 是条件成立时为 1、否则为 0 的指示函数。

$\rho_e$ 是到最近合法区间的残差，与 $\phi_e$ 不是同一个函数。
无穷上下界对应项不参与 max。

定义 $R(z)=\operatorname{llround}(z)$：就近取整数，恰好半整数时远离零。
例如 $R(0.5)=1,\ R(-0.5)=-1$：$R$ 是上述取整函数，正负半整数都向远离零的一侧取整；不能替换成 ties-to-even。

**公式 22：从上一轮到本轮中间状态更新停滞计数。**

$$
\begin{aligned}
\mathrm{moved}_i
 &=\mathbf1\{R(W\widehat u_i)\ne R(Wu_i^{(k)})\},\\
\widehat c_e
 &=\begin{cases}
\min\{3,c_e^{(k)}+1\},
 &\neg\mathrm{moved}_{s_e}\land\neg\mathrm{moved}_{t_e}
   \land V_e(\mathbf u^{(k)})\land V_e(\widehat{\mathbf u}),\\
0,&\text{otherwise}.
\end{cases}
\end{aligned}
$$

**本公式的符号说明：**

- $u_i^{(k)}$ 是上一轮的累计位移，$\widehat u_i$ 是本轮梯度阶段的累计位移，W 是线宽。
- R 是 llround 取整，恰好半整数时远离零；$\mathrm{moved}_i$ 表示两次累计位移的取整结果是否不同。
- e 是边编号，$s_e,t_e$ 是两端编号；$c_e^{(k)}$ 是旧停滞计数，$\widehat c_e$ 是计数更新后、跳跃前的中间计数。
- $V_e(\mathbf u^{(k)})$ 和 $V_e(\widehat{\mathbf u})$ 分别判断旧状态和中间状态是否仍达到残差门槛。
- $\neg$ 表示不成立，$\land$ 表示全部条件同时满足；$\mathbf1$ 把真、假转为 1、0。
- $\min\{3,c_e^{(k)}+1\}$ 连续增加计数并将其封顶于 3；否则归零。

**本公式在做什么：** 只有两端显示位置都不变且更新前后仍明显违规，才算又停滞了一整轮。比较的是累计位移取整值，不是本轮步长的取整值。中间计数还可能被后续成功跳跃清零，所以它不一定等于最终输出计数。


先对全部边计算 $\widehat{\mathbf c}$，之后才进入跳跃。
这里比较累计位移的取整结果；不是把本轮增量取整，也不是把绝对中心取整。
源码：[StallRepair.cpp:112](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:112)。

### 5.2 按边顺序尝试跳跃

令工作坐标 $\mathbf w=\widehat{\mathbf u}$，工作计数 $\mathbf c=\widehat{\mathbf c}$。
按 `layer.arcs` 的顺序遍历 e；建图默认按 `(type,src,dst)` 排序。
只有访问该边时 $c_e=3$ 才调用跳跃。

**每次跳跃读取当时的工作坐标 w，而不是全部使用同一个 û 快照。**
前一条边成功后会修改 w 和关联边计数，后面的边看到的是修改后的状态。
源码：[Arcs.cpp:177](/workspace/openroad-26/src/del/src/graph/Arcs.cpp:177)。

计算当前 $d=d_e(\mathbf w)$，峰值位置 $\beta$：

**公式 23：当前违规区间的势垒峰值。**

$$
\beta=
\begin{cases}
\theta_B/2,&\mathrm{type}=B,\quad0<d<\theta_B,\\
\operatorname{sign}(d)\theta_C/2,
 &\mathrm{type}=C,\quad d\ne0,\quad|d|<\theta_C.
\end{cases}
$$

**本公式的符号说明：**

- d 是访问这条边时工作坐标算出的当前归一化距离，可能已被前面的跳跃改变。
- β 是当前势垒峰值对应的距离；type 是当前边类型。
- $\theta_B,\theta_C$ 是 B/C 阈值；除以 2 得到势垒区间的中点。
- sign(d) 在 d 为正时取 +1、为负时取 -1；绝对值判断 C 是否位于正侧或负侧禁区。

**本公式在做什么：** 只有 B 的正距离禁区和 C 的非零禁区具有可跨越峰值。A 或负距离 B 使用单侧罚函数，没有这类峰值，因此这一跳跃规则对它们返回失败。


不满足上述任一条件时，跳跃失败，不修改坐标和计数。

为避免混淆，$\beta$ 是势垒峰值，$m$ 是梯度步长上限。

目标侧 $\sigma$ 的尝试序列为：

**公式 24：先尝试跨到峰值的哪一侧。**

$$
\sigma_{\mathrm{first}}=
\begin{cases}+1,&d\le\beta,\\-1,&d>\beta.\end{cases}
$$

**本公式的符号说明：**

- d 是当前边距离，β 是势垒峰值。
- $\sigma_{\mathrm{first}}$ 是首次尝试的目标侧标签：+1 表示目标在峰值较大距离一侧，-1 表示在较小距离一侧。
- d≤β 与 d>β 决定跨向相反侧；这个 σ 不是端点实际移动方向 ν。

**本公式在做什么：** 目标是跨过峰值而不只是向当前下坡方向继续移动。恰在峰值时先尝试较大距离一侧，无法实现才尝试较小距离一侧；不在峰值时只尝试一个方向。


若 $d\ne\beta$，只尝试 $\sigma_{\mathrm{first}}$；若 $d=\beta$，先尝试 $+1$，失败后尝试 $-1$。

**公式 25：由目标距离算出需要分摊的变化量。**

$$
\begin{aligned}
d^\ast&=\beta+\sigma\eta,&
\chi&=d^\ast-d,\\
\nu&=\begin{cases}+1,&\chi>0,\\-1,&\chi\le0,\end{cases}&
T&=|\chi|.
\end{aligned}
$$

**本公式的符号说明：**

- β 是峰值，σ 是本次选定目标侧，η 是相对峰值的归一化目标偏移。
- $d^\ast$ 是这次跳跃要实现的目标边距离；星号标记目标，不表示乘法或最优性证明。
- d 是当前边距离，χ 是目标减当前的有符号变化量。
- ν 是改变边距离的方向：+1 要增加距离，-1 要减小距离；T 是变化幅度，等于 χ 的绝对值。

**本公式在做什么：** 跳跃偏移 η 决定目标在峰值另一侧多远；真正需要改变的边距离是目标减当前，两者通常不同。这个变化量要由两端反向移动合计实现。


两端容量：

**公式 26：两端分别还能够移动多少。**

$$
(C_s,C_t)=
\begin{cases}
(w_s-L_s,\ U_t-w_t),&\nu>0,\\
(U_s-w_s,\ w_t-L_t),&\nu<0,
\end{cases}
$$

**本公式的符号说明：**

- s、t 是当前边的 src、dst 编号，$w_s,w_t$ 是访问该边时的工作累计归一化位移。
- $L_s,U_s,L_t,U_t$ 是两端累计位移的固定域上下界。
- $C_s,C_t$ 是两端沿本次所需方向还能够提供的位移容量，单位为归一化位移。
- ν 是改变边距离的方向。ν>0 时 src 向负方向、dst 向正方向；ν<0 时反过来。

**本公式在做什么：** 先看移动域是否允许实现目标。增加距离时，src 的容量取到下界的距离、dst 的容量取到上界的距离；减少距离时使用另外两侧。不能只看某个端点是否可动。


**公式 27：src 分配量的可行范围与度数偏好。**

$$
A_{\min}=\max(0,T-C_t),\qquad
A_{\max}=\min(T,C_s),\qquad
A_{\mathrm{preferred}}=\frac{Tq_t}{q_s+q_t}.
$$

**本公式的符号说明：**

- T 是两端合计要贡献的距离变化幅度，$C_s,C_t$ 是两端容量。
- $A_{\min}$ 是 src 至少要贡献的量：dst 提供不完的部分必须由 src 提供。
- $A_{\max}$ 是 src 最多可贡献的量，同时不能超过总需求 T 或自身容量。
- $A_{\mathrm{preferred}}$ 是按连接数得到的偏好分配；$q_s,q_t$ 是两端的关联边数加一。
- 此处字母 A 指 src 的分配量，不是梯度阶段的试算编号 A，也不是 A 类边。

**本公式在做什么：** 先确定联合可行区间，再考虑分摊偏好。src 连接数越大，它的偏好贡献越小；若某端容量紧，另一端承担更多。可行下界超过上界意味着两端合计容量不足。


$\nu>0$ 时 src 向低处、dst 向高处；$\nu<0$ 时 src 向高处、dst 向低处。

src 分配量 $A$ 在可行区间内尽量接近 $A_{\mathrm{preferred}}$，dst 分配量为 $T-A$：

**公式 28：计算并联合提交两端新位置。**

$$
\begin{aligned}
A&=\operatorname{clip}_{[A_{\min},A_{\max}]}(A_{\mathrm{preferred}}),\\
w_s'&=\operatorname{clip}_{[L_s,U_s]}(w_s-\nu A),\\
w_t'&=\operatorname{clip}_{[L_t,U_t]}(w_t+\nu(T-A)).
\end{aligned}
$$

**本公式的符号说明：**

- A 是最终给 src 的归一化贡献，$A_{\min},A_{\max},A_{\mathrm{preferred}}$ 是其可行范围与偏好。
- $w_s,w_t$ 是本次跳跃前的工作位置；$w_s',w_t'$ 是本次跳跃后的候选工作位置。这里撇号表示新值，不表示求导。
- $L_s,U_s,L_t,U_t$ 是两端域，clip 保证候选仍在各自域内。
- ν 是距离变化方向，T-A 是给 dst 的贡献；两端相反移动，贡献相加改变边距离。

**本公式在做什么：** 先把分配量限制到可行区间，再得到两端候选。源端负号和目标端正号来自 dst-src 的距离定义。两端必须一起通过检查、一起提交，失败时不能留下半次移动。


高度连接端点移动较少。这里的 q 和梯度阶段完全相同，都是度数加 1。
源码：[StallRepair.cpp:32](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:32)。

### 5.3 源码浮点可行性检查与联合提交

为精确对应源码，定义 $\mu$ 为 double 的机器 epsilon：

**公式 29：浮点联合可行性的容差。**

$$
\mathrm{tol}=16\mu\max\{1,T,|d|,|d^\ast|,|w_s|,|w_t|\}.
$$

**本公式的符号说明：**

- tol 是这次跳跃用于可行性和目标误差判断的归一化数值容差。
- μ 是 double 机器 epsilon，表示浮点相对精度量级；不是前面平滑尺度 ε。
- T 是所需距离变化幅度，d 是当前距离，$d^\ast$ 是目标距离，$w_s,w_t$ 是两端工作位移。
- max 选取本次相关数值的最大尺度，绝对值去除正负号；常数 1 保证小数值时也有基准尺度，16 是源码容差倍数。

**本公式在做什么：** 这里只容忍浮点计算带来的极小边界误差，不是允许额外几何违规的工艺容差。若域不足远超此量级，跳跃必须失败；最终实际距离也要再次检查。


若 $A_{\min}>A_{\max}+\mathrm{tol}$，该目标失败。
若仅因浮点误差有 $A_{\min}>A_{\max}$，源码令 $A=A_{\max}$；否则按正常 clip 公式。
重算 $d_{\mathrm{after}}=\frac{\mathbf a\cdot(\mathbf p_t^{(0)}-\mathbf p_s^{(0)})}{W}+w_t'-w_s'$，仅当以下条件全部成立才提交：

**公式 30：跳跃提交前的最后检查。**

$$
w_s',w_t'\in\mathbb R\ \text{and finite},
\qquad
(d_{\mathrm{after}}-\beta)\sigma>0,
\qquad
|d_{\mathrm{after}}-d^\ast|\le\mathrm{tol}.
$$

**本公式的符号说明：**

- $w_s',w_t'$ 是两端候选工作位移；属于实数且 finite 要求不是 NaN 或无穷值。
- $d_{\mathrm{after}}$ 是由两端候选重新算出的真实边距离，β 是峰值，σ 是本次目标侧。
- $(d_{\mathrm{after}}-\beta)\sigma>0$ 要求结果严格在所选峰值另一侧。
- $d^\ast$ 是本次目标距离，tol 是机器精度容差；绝对差不得超过 tol。

**本公式在做什么：** 即使容量分配看起来可行，也要用实际候选重新验算。只有两端有限、确实跨过峰值且达到目标距离，才同时写入工作坐标；这些检查没有要求能量下降。


失败不能只更新一个端点；成功同时更新两端。
对每个实际改变的端点 $i$，令它的全部关联边 $c_f=0,\quad f\in\mathcal I_i$。这里 $f$ 是与端点 $i$ 相连的边编号，$\mathcal I_i$ 是这些边的编号集合，$c_f$ 是当前工作停滞计数；成功移动后清零，避免同一移动端点本轮重复跳跃。
后续边是否有资格跳跃，要检查此时的 c，不能在开始时固定一个候选边列表后无条件全部执行。
失败的边保留计数 3，下一轮若继续符合停滞条件仍可能尝试。

遍历完成后，令 $\mathbf u^{(k+1)}=\mathbf w,\quad\mathbf c^{(k+1)}=\mathbf c$。
跳跃不调用能量下降条件，也不受 $m$ 限制；$\eta$ 只是相对峰值的目标偏移，不是本轮节点位移上限。
源码：[StallRepair.cpp:65](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:65)、[StallRepair.cpp:149](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:149)。

## 6. 单轮更新的完整伪代码

下面的过程等价表达当前算法控制流，使用上文已完整定义的能量、梯度、残差和跳跃公式。
它是阅读用伪代码，不是新增的 C++/CUDA 实现。

```text
function UPDATE(P, c, C, k):
    # 0. 当前绝对XY坐标 → 相对原始中心的累计归一化位移
    u[i] = dot(C.a, P[i]-C.P0[i])/C.W
    λ = 10 * 2^min(3, floor(k/25))
    E0 = energy(u, λ)                         # 第3节
    rule0 = sum_e φ_e(d_e(u))
    g = gradient(u, λ)                        # 第3.3节

    # 1. 构建方向；这里只计算一次
    v[i] = clip(-g[i]/q[i], L[i]-u[i], U[i]-u[i])
    M = max_i abs(v[i])                       # 空层按0处理
    S = m/M if M>m else 1
    u_hat = copy(u)
    accepted = false

    # 2. 整层回溯；各候选都从u出发
    for A in [0,1,2]:
        for b in [0,...,19]:
            j[i] = 0 if A==0 else (0.01 if (i+A)%2==0 else -0.01)
            z[i] = clip(u[i]+clip(S*2^(-b)*(v[i]+j[i]), -m,m), L[i],U[i])
            if not any_i(abs(z[i]-u[i])>1e-12):
                break                        # 结束当前A的回溯
            if energy(z,λ) < E0-1e-12*max(1,E0):
                u_hat = z
                accepted = true
                break
        if accepted:
            break
        if rule0==0:
            break

    # 3. 所有边先计数；此时还不允许修改u_hat
    moved[i] = llround(W*u_hat[i]) != llround(W*u[i])
    c_work = copy(c)
    for e in original_arc_order:
        s,t = endpoints(e)
        if moved[s] or moved[t] or not V_e(u) or not V_e(u_hat):
            c_work[e] = 0
        else:
            c_work[e] = min(3,c[e]+1)

    # 4. 按原边顺序跳跃，后面的边看到前面修改后的w与计数
    w = copy(u_hat)
    for e in original_arc_order:
        if c_work[e]<3:
            continue
        s,t = endpoints(e)
        old_s,old_t = w[s],w[t]
        ok,new_s,new_t = TRY_JUMP(e,w,C)      # 第5.2、5.3节全部公式
        if not ok:
            continue
        w[s],w[t] = new_s,new_t              # 同时提交
        for i in [s,t]:
            old_i = old_s if i==s else old_t
            if w[i] != old_i:
                for f in I[i]:
                    c_work[f] = 0

    # 5. 重建连续绝对XY坐标与本轮二维增量
    Pnext[i] = P0[i] + W*a*w[i]
    deltaP[i] = W*a*(w[i]-u[i])
    return Pnext, c_work, deltaP
```

与源码对应的最小动态状态也可以只保存 $(\mathbf u,\mathbf c)$；XY 是它的输入／输出表达形式。
多层情况分别调用 UPDATE，每层有自己的 W、方向、图和全层回溯；结果拼接即可。
不能把所有层合在一个最大方向 M 或一个接受条件中，还声称保持当前更新函数。

## 7. 两个手算例子

### 7.1 直接从 XY 算一轮正常更新

水平层，$\mathbf a=(1,0)$，$W=10\ \mathrm{DBU}$，当前是第一轮之前：

**公式 31：正常更新例子的输入。**

$$
\begin{aligned}
\mathbf p_s^{(0)}=\mathbf p_s^{(k)}&=(100,200),&
\mathbf p_t^{(0)}=\mathbf p_t^{(k)}&=(140,200),\\
\theta_A&=5,&
[L_s,U_s]&=[-2,0],\\
[L_t,U_t]&=[0,2],&
q_s=q_t&=2,\\
m&=0.25,&
\lambda&=10,\qquad c_e^{(k)}=0.
\end{aligned}
$$

**本公式的符号说明：**

- s、t 是唯一 A 边的两端，$\mathbf p_s^{(0)},\mathbf p_t^{(0)}$ 是初始中心，带 (k) 的是本轮输入中心；本例 k=0，因此两者相同。
- 坐标 (100,200)、(140,200) 的单位是 DBU；W=10 DBU、方向为水平层。
- $\theta_A=5$ 是归一化最小中心距离；两组 [L,U] 是 src 与 dst 允许的累计位移范围。
- $q_s=q_t=2$ 表示各自有一条关联边再加 1；m=0.25 是梯度增量上限。
- λ=10 是本轮规则权重，$c_e^{(k)}=0$ 是唯一边的旧停滞计数。

**本公式在做什么：** 这是从实际 XY 点出发的第一轮输入。两端可以向彼此远离的方向移动，初始中心距为 40 DBU，小于要求的 50 DBU。


图中只有一条 A 边 $s\to t$。

这些域与自身金属限制下可向两端分离移动的情形相容。

**公式 32：从输入点计算梯度与缩放。**

$$
\begin{aligned}
u_s=u_t&=0,&d&=\frac{140-100}{10}=4,\\
\phi&=(4-5)^2=1,&\phi'&=2(4-5)=-2,\\
g_s&=10(-1)(-2)=20,&g_t&=10(+1)(-2)=-20,\\
v_s&=\operatorname{clip}_{[-2,0]}(-20/2)=-2,&
v_t&=\operatorname{clip}_{[0,2]}(20/2)=2,\\
M&=2,&S&=0.25/2=0.125.
\end{aligned}
$$

**本公式的符号说明：**

- $u_s,u_t$ 是旧累计归一化位移，本例都为零；d 是旧距离除以 W。
- φ 是唯一 A 边势能，φ′ 是其距离导数，使用阈值 5。
- $g_s,g_t$ 是两端梯度，计算中的 -1/+1 是 src/dst 的关联系数，10 是本轮权重。
- $v_s,v_t$ 是梯度除度数并按两端域截断后的方向；除数 2 是 q，clip 的区间是剩余可移动位移。
- M=2 是最大方向幅度，S=0.25/2 将整层最大梯度增量限制到 m=0.25。

**本公式在做什么：** 负距离导数使 src 梯度为正、dst 梯度为负，因此负梯度分别向左和向右。未限制方向幅度本来为 10，但域只允许幅度 2，所以先截断，再乘 0.125。


第一个候选 $A=0,b=0$ 为：

**公式 33：正常更新例子的第一个候选。**

$$
z_s=-0.25,\qquad z_t=+0.25.
$$

**本公式的符号说明：**

- $z_s,z_t$ 是试算 A=0、回溯 b=0 的两端候选累计归一化位移。
- -0.25 表示 src 向左累计移动 0.25W，+0.25 表示 dst 向右累计移动 0.25W。
- s、t 仍指同一条边的两个 trim，z 在能量判定前只是候选值。

**本公式在做什么：** 乘以 W=10 后，候选实际位移为 -2.5/+2.5 DBU。候选通过整层能量下降条件后才成为梯度阶段接受的位置。


原能量为 10。新距离为 4.5，新能量约为 2.504803998，符合接受条件。
两端累计位移的 llround 结果从 0 变成 -3/+3，停滞计数清零，无跳跃。

**公式 34：正常更新例子的下一轮坐标。**

$$
\begin{aligned}
\mathbf p_s^{(k+1)}
 &=(100,200)+10(1,0)(-0.25)=(97.5,200),\\
\mathbf p_t^{(k+1)}
 &=(140,200)+10(1,0)(+0.25)=(142.5,200),\\
\Delta\mathbf p_s&=(-2.5,0)\ \mathrm{DBU},\\
\Delta\mathbf p_t&=(+2.5,0)\ \mathrm{DBU}.
\end{aligned}
$$

**本公式的符号说明：**

- $\mathbf p_s^{(k+1)},\mathbf p_t^{(k+1)}$ 是本轮结束后的两端中心点，单位 DBU。
- (100,200)、(140,200) 是本例初始也是旧中心；10 是 W，(1,0) 是水平移动方向。
- -0.25/+0.25 是本例最终累计归一化位移，没有额外跳跃。
- $\Delta\mathbf p_s,\Delta\mathbf p_t$ 是本轮二维增量，等于新中心减旧中心；符号 Δ 表示变化量。

**本公式在做什么：** 把归一化位移还原成实际 XY，src 从 X=100 到 97.5，dst 从 X=140 到 142.5，Y 均为 200。新中心差为 45 DBU，仍未达到 50 DBU，需要后续迭代继续处理。


若相同问题旋转到垂直层，公式只需取 $\mathbf a=(0,1)$，X 不动，更新 Y。

### 7.2 同样 XY 输入，不同停滞计数，得到不同的下一轮坐标

为单独验证 UPDATE，构造一个允许双侧移动的 LayerOptimizer 输入；这是算子例子，不宣称它是构图器默认产生的自身金属移动域。
水平层、$W=10\ \mathrm{DBU}$、$\lambda=10$、单条 B 边：

**公式 35：停滞历史对照例子的共同输入。**

$$
\begin{aligned}
\mathbf p_s^{(0)}=\mathbf p_s^{(k)}&=(100,200),&
\mathbf p_t^{(0)}=\mathbf p_t^{(k)}&=(125,200),\\
[L_s,U_s]=[L_t,U_t]&=[-2,2],&
q_s=q_t&=2,\\
u_s=u_t&=0,&
d&=2.5,\quad\theta_B=5,\\
m&=0.25,&\tau&=5\ \mathrm{DBU},\quad\eta=0.01.
\end{aligned}
$$

**本公式的符号说明：**

- s、t 是唯一 B 边的两端，初始与本轮输入中心分别为 (100,200)、(125,200)，单位 DBU。
- 两端 [L,U] 均为 [-2,2]，表示这个构造例子允许双侧移动；W=10 DBU，水平层方向为 (1,0)。
- $q_s,q_t=2$ 是连接数加一，$u_s,u_t=0$ 表示输入还在初始中心。
- d=2.5 是当前归一化距离，$\theta_B=5$ 是 B 阈值，所以当前正好在禁区峰值。
- m=0.25 是梯度增量上限，τ=5 DBU 是修复残差门槛，η=0.01 是跨峰值目标偏移；本轮 λ=10。

**本公式在做什么：** 两种情况保持这些输入完全相同，仅改变旧停滞计数。这样可以单独展示历史如何影响下一轮坐标，不把差异混入图或移动域。


例如 $J_{\mathrm{nm}}=0.1$ nm，$D_{\mathrm{DBU}/\mu\mathrm m}=1000$ 时，$\eta=0.01$。

位于势垒峰值，两端 g=0，所以 v=0，M=0，S=1。
A=0 的候选不移动，退出正常试算；规则能量不为零，进入 A=1。
A=1 时节点0的 jitter 为 -0.01，节点1为 +0.01：

**公式 36：峰值处由扰动得到的中间状态。**

$$
\widehat u_s=-0.01,\qquad
\widehat u_t=+0.01,\qquad
d_{\mathrm{hat}}=2.52.
$$

**本公式的符号说明：**

- $\widehat u_s,\widehat u_t$ 是梯度阶段接受的两端累计归一化位移，帽子表示尚未进行本轮跳跃。
- -0.01/+0.01 来自 A=1 的交替扰动；两端旧位移均为零。
- $d_{\mathrm{hat}}$ 是中间状态的边距离；下标 hat 是标签，表示使用帽子位移求距离。

**本公式在做什么：** 正常梯度为零，正常候选不动；扰动却将归一化距离从 2.5 改成 2.52。实际端点只移动 -0.1/+0.1 DBU，取整显示仍未改变。


**公式 37：扰动候选的能量验算。**

$$
\begin{aligned}
E_{\mathrm{old}}&=10,\\
E_{\mathrm{candidate}}
 &=10\cdot16\cdot0.504^2\cdot0.496^2
   +2\cdot0.01\left(\sqrt{0.01^2+0.01^2}-0.01\right)\\
 &\approx9.998802884<10.
\end{aligned}
$$

**本公式的符号说明：**

- $E_{\mathrm{old}}$ 是旧状态总能量，$E_{\mathrm{candidate}}$ 是当前扰动候选总能量；下标均为状态标签。
- 10 是规则权重 λ；16 是势垒系数；0.504 是候选距离 2.52 除以阈值 5 得到的区间比例 t，0.496 是 1-t。
- 第二项中的 2 表示两个端点，外侧 0.01 是 α，平方根中第一个 0.01 是位移幅度，第二个 0.01 是 ε。
- 所有平方为普通乘方，≈ 表示数值近似；严格小于 10 表示候选总能量下降。

**本公式在做什么：** 扰动略降低规则势能，虽然增加了一点累计位移正则项，但合计仍下降，且超过源码的数值接受门槛，因此接受这个中间状态。


两端物理累计位移是 -0.1/+0.1 DBU，llround 后仍为0，故仍被判定为可见静止。
更新前后残差均大于5 DBU。

情况一：输入 $c_e^{(k)}=0$，停滞计数更新到 1，不跳跃。

**公式 38：旧计数为零时的下一轮结果。**

$$
\mathbf p_s^{(k+1)}=(99.9,200),\qquad
\mathbf p_t^{(k+1)}=(125.1,200),\qquad
c_e^{(k+1)}=1.
$$

**本公式的符号说明：**

- $\mathbf p_s^{(k+1)},\mathbf p_t^{(k+1)}$ 是输入旧计数为 0 时，本轮最终两端中心，单位 DBU。
- 99.9 和 125.1 分别是原 X 加 -0.1 和 +0.1 DBU，两个 Y 仍为 200。
- $c_e^{(k+1)}=1$ 是本轮输出停滞计数，e 指唯一 B 边；本轮满足停滞条件，但只累计到 1。

**本公式在做什么：** 由于尚未达到三轮停滞门槛，不进行跳跃，最终位置就是扰动接受的位置。虽然 solver 坐标发生小变化，取整显示未动仍会累计停滞。


情况二：输入 $c_e^{(k)}=2$，停滞计数更新到 3，执行跳跃。

**公式 39：旧计数为二时的跳跃计算。**

$$
\begin{aligned}
d&=2.52,&\beta&=2.5,&\sigma&=-1,\\
d^\ast&=2.49,&\chi&=-0.03,&\nu&=-1,\quad T=0.03,\\
A_{\mathrm{preferred}}&=\frac{0.03\cdot2}{2+2}=0.015,\\
u_s^{(k+1)}&=-0.01+0.015=+0.005,\\
u_t^{(k+1)}&=+0.01-0.015=-0.005.
\end{aligned}
$$

**本公式的符号说明：**

- d=2.52 是扰动后的工作距离，β=2.5 是峰值；σ=-1 表示目标在峰值较小距离侧。
- $d^\ast=2.49$ 是目标，χ=-0.03 是所需距离变化，ν=-1 表示缩小距离，T=0.03 是变化幅度。
- $A_{\mathrm{preferred}}$ 是给 src 的偏好贡献；分子和分母中的 2 是两端 q，故两端平分 0.03。
- $u_s^{(k+1)},u_t^{(k+1)}$ 是跳跃后的最终累计归一化位移；原来的 -0.01/+0.01 是中间位移，±0.015 是跳跃修正。

**本公式在做什么：** 旧计数 2 在本轮增长到 3，触发跳跃。两端反向修正各 0.015W，距离从 2.52 跨过峰值到 2.49，因此最终位移与仅扰动的情况不同。


**公式 40：成功跳跃后的下一轮中心点。**

$$
\mathbf p_s^{(k+1)}=(100.05,200),\qquad
\mathbf p_t^{(k+1)}=(124.95,200).
$$

**本公式的符号说明：**

- $\mathbf p_s^{(k+1)},\mathbf p_t^{(k+1)}$ 是输入旧计数为 2 时，本轮完成梯度与跳跃后的最终中心，单位 DBU。
- 100.05 来自初始 X=100 加最终累计位移 10×0.005；124.95 来自初始 X=125 加 10×(-0.005)。
- 两个 Y 坐标均为 200，因为是水平层；k+1 表示这组坐标作为下一轮输入。

**本公式在做什么：** 同样旧 XY 点由于停滞历史不同，最终得到不同新点。这里 src 相对于本轮旧位置向右 0.05 DBU，dst 向左 0.05 DBU；不能把跳跃修正 0.15 DBU 当作整个迭代的总增量。


两端发生实际改变，关联边计数清零，输出 $c_e^{(k+1)}=0$。


这组例子说明相同 $(x,y)$、相同图、相同轮号，因 c 不同可以产生不同的最终坐标。
也说明“峰值梯度为零”并不等同于“整轮不动”，因为原代码会尝试 jitter。
第二种情况下，跳跃把距离从2.52改到2.49；它只保证跨峰值目标和域可行，不要求最终能量低于梯度阶段结果。

## 8. 如何把公式对照到原始代码

| 形式化内容 | 原始源码位置 |
|---|---|
| u 初始化、L/U、q、邻接边、c | [Optimizer.cpp:45](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:45) |
| LB/F 势函数及导数 | [Optimizer.cpp:15](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:15) |
| E 与 g，src 减／dst 加 | [Optimizer.cpp:89](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:89) |
| v、M、S | [Optimizer.cpp:156](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:156) |
| jitter 与候选 z | [Optimizer.cpp:166](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:166) |
| moved 判断与候选接受 | [Optimizer.cpp:180](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:180) |
| d_e | [Optimizer.cpp:203](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:203) |
| 完整一轮、保存旧值与重算最终增量统计 | [Optimizer.cpp:212](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:212) |
| 残差 V_e | [StallRepair.cpp:14](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:14) |
| β、η、目标、容量、度数分配、联合提交 | [StallRepair.cpp:32](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:32) |
| c 更新与有序跳跃、关联计数重置 | [StallRepair.cpp:112](/workspace/openroad-26/src/del/src/optimize/StallRepair.cpp:112) |
| λ 与每层执行 | [RunOptimization.cpp:240](/workspace/openroad-26/src/del/src/optimize/RunOptimization.cpp:240) |

源码链接指向本工作区的绝对路径；标签和链接中均包含对应行号。

### 输出坐标和程序显示坐标的区别

下面的输出说明中，$i$ 是 trim 编号，$W$ 是线宽，$\mathbf a$ 是移动方向，$u_i^{(k)}$ 与 $u_i^{(k+1)}$ 分别是旧、新累计归一化位移。UPDATE 输出连续中心，真实本轮增量是 $W\mathbf a\,(u_i^{(k+1)}-u_i^{(k)})$。
`displacementDbu()` 输出累计位移 $Wu_i^{(k+1)}$。
GUI 则对累计位移做 llround，再平移原始矩形；其中用于标记的 `center(node)` 还会将原始半 DBU 中心截断。
因此不能从 GUI 整数坐标无损恢复本轮 solver 输入。
源码：[Optimizer.cpp:237](/workspace/openroad-26/src/del/src/optimize/Optimizer.cpp:237)、[OptimizationGraphics.cpp:94](/workspace/openroad-26/src/del/src/OptimizationGraphics.cpp:94)。

本文是从源码提炼的一轮形式化接口，精确保留了条件分支与状态依赖；
数学上的 XY 重建与源码直接保存 u 在浮点运算次序上可能有差异，实际实现宜持续保存 u，XY 用于输入初始化与输出展示。
LP/MIP 后处理是迭代预算完成后的另一个算子，未合并进 UPDATE，详见同目录 `runtime_and_gpu_analysis_zh.md`。
