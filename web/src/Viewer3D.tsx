import { schemeTableau10 } from 'd3-scale-chromatic'
import { useEffect, useMemo, useRef, useState } from 'react'
import { Box, Button, Group, Paper, Select, Stack, Text, UnstyledButton } from '@mantine/core'
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import type { ContainerResult, PalletResult, Placement } from './api'

type ColorMode = 'type' | 'platform' | 'group'
type ViewName = 'default' | 'front' | 'back' | 'left' | 'right' | 'top' | 'bottom'

const palette = schemeTableau10
const categoryOf = (placement: Placement, mode: ColorMode) => String(mode === 'type' ? placement.box_type_id : placement[mode] || '(none)')
const containerGap = 20

function placementFromObject(object: THREE.Object3D | undefined): Placement | null {
  let current: THREE.Object3D | null | undefined = object
  while (current) {
    const placement = current.userData.placement as Placement | undefined
    if (placement) return placement
    current = current.parent
  }
  return null
}

function Viewer3D({ containers, boxTypes, pallets = [] }: {
  containers: ContainerResult[]
  boxTypes: Array<{ id: string; sx: number; sy: number; sz: number }>
  pallets: PalletResult[]
}) {
  const mountRef = useRef<HTMLDivElement>(null)
  const sceneRef = useRef<{ scene: THREE.Scene; camera: THREE.PerspectiveCamera; controls: OrbitControls; renderer: THREE.WebGLRenderer } | null>(null)
  const [containerIndex, setContainerIndex] = useState(-1)
  const [colorMode, setColorMode] = useState<ColorMode>('type')
  const [view, setView] = useState<ViewName>('default')
  const [visibleKeys, setVisibleKeys] = useState<string[]>([])
  const [resetToken, setResetToken] = useState(0)
  const [hoveredPlacement, setHoveredPlacement] = useState<Placement | null>(null)
  const [activePallet, setActivePallet] = useState<{ pallet: PalletResult; placement: Placement; containerIndex: number } | null>(null)
  const palletMap = useMemo(() => new Map(pallets.map((pallet) => [pallet.pallet_id, pallet])), [pallets])

  const visibleEntries = useMemo(() => containerIndex < 0
    ? containers.map((container, index) => ({ container, index }))
    : containers[containerIndex] ? [{ container: containers[containerIndex], index: containerIndex }] : [], [containers, containerIndex])
  const selectedContainer = containerIndex >= 0 ? containers[containerIndex] : undefined

  useEffect(() => {
    if (!mountRef.current) return
    const mount = mountRef.current
    const scene = new THREE.Scene()
    scene.background = new THREE.Color('#101923')
    const camera = new THREE.PerspectiveCamera(42, mount.clientWidth / Math.max(1, mount.clientHeight), 0.1, 1_000_000)
    camera.position.set(5, 5, 5)
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false })
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    renderer.setSize(mount.clientWidth, mount.clientHeight)
    renderer.outputColorSpace = THREE.SRGBColorSpace
    mount.appendChild(renderer.domElement)
    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    scene.add(new THREE.HemisphereLight('#dce9ed', '#1d2834', 2.4))
    const keyLight = new THREE.DirectionalLight('#fff4dc', 2.5)
    keyLight.position.set(3, 6, 4)
    scene.add(keyLight)
    sceneRef.current = { scene, camera, controls, renderer }
    const resize = () => {
      if (!mountRef.current) return
      camera.aspect = mountRef.current.clientWidth / Math.max(1, mountRef.current.clientHeight)
      camera.updateProjectionMatrix()
      renderer.setSize(mountRef.current.clientWidth, mountRef.current.clientHeight)
    }
    const observer = new ResizeObserver(resize)
    observer.observe(mount)
    let frame = 0
    const animate = () => {
      controls.update()
      renderer.render(scene, camera)
      frame = requestAnimationFrame(animate)
    }
    animate()
    return () => {
      cancelAnimationFrame(frame)
      observer.disconnect()
      controls.dispose()
      renderer.dispose()
      mount.removeChild(renderer.domElement)
      sceneRef.current = null
    }
  }, [])

  useEffect(() => {
    const runtime = sceneRef.current
    if (!runtime) return
    const { scene, camera } = runtime
    const old = scene.getObjectByName('pack3d-result')
    if (old) scene.remove(old)
    const root = new THREE.Group()
    root.name = 'pack3d-result'
    // 托盘单元位置索引（pallet_id → 所在容器与放置），供点击/内部渲染使用
    const palletInfo = new Map<string, { placement: Placement; containerIndex: number }>()
    visibleEntries.forEach(({ container, index }) => {
      container.placements.forEach((placement) => {
        if (palletMap.has(placement.box_id)) palletInfo.set(placement.box_id, { placement, containerIndex: index })
      })
    })
    const categoryValues = visibleEntries.flatMap(({ container }) => container.placements
      .filter((placement) => !palletMap.has(placement.box_id))
      .map((placement) => categoryOf(placement, colorMode)))
    const categories = [...new Set(categoryValues)]
    const colors = new Map(categories.map((value, index) => [value, palette[index % palette.length]]))
    const rayTargets: THREE.Object3D[] = []
    const geometry = new THREE.BoxGeometry(1, 1, 1)
    const edgeGeometry = new THREE.EdgesGeometry(geometry)
    let offsetX = 0
    visibleEntries.forEach(({ container, index }) => {
      const dimensions = { x: container.sx, y: container.sy, z: container.sz }
      const frame = new THREE.LineSegments(
        new THREE.EdgesGeometry(new THREE.BoxGeometry(dimensions.x, dimensions.z, dimensions.y)),
        new THREE.LineBasicMaterial({ color: '#91a5ad', transparent: true, opacity: 0.75 }),
      )
      frame.position.set(offsetX + dimensions.x / 2, dimensions.z / 2, dimensions.y / 2)
      root.add(frame)
        ; container.obstacles.forEach((obstacle) => {
          const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: '#e34d3a', transparent: true, opacity: 0.35, roughness: 0.5, metalness: 0.1 }))
          mesh.add(new THREE.LineSegments(edgeGeometry, new THREE.LineBasicMaterial({ color: '#8c2f22', transparent: true, opacity: 0.6 })))
          mesh.scale.set(obstacle.dx, obstacle.dz, obstacle.dy)
          mesh.position.set(offsetX + obstacle.x + obstacle.dx / 2, obstacle.z + obstacle.dz / 2, obstacle.y + obstacle.dy / 2)
          root.add(mesh)
        })
        ; container.facets.forEach((facet) => {
          const axes = [
            { key: 'dx' as const, extent: container.sx },
            { key: 'dy' as const, extent: container.sy },
            { key: 'dz' as const, extent: container.sz },
          ]
          const present = axes.filter((a) => facet[a.key] !== undefined && facet[a.key] !== 0)
          if (present.length !== 2) return
          const [u, v] = present
          const w = axes.find((a) => !present.includes(a))
          if (!w) return
          const su = facet[u.key] ?? 0
          const sv = facet[v.key] ?? 0
          const cornerU = su > 0 ? u.extent : 0
          const cornerV = sv > 0 ? v.extent : 0
          // 斜面矩形面两个端点：A 在 u 墙上（距 v 角 sv），B 在 v 墙上（距 u 角 su）；沿 w 贯穿
          const toThree = (cu: number, cv: number, cw: number) => {
            const coords: Record<string, number> = { dx: 0, dy: 0, dz: 0 }
            coords[u.key] = cu
            coords[v.key] = cv
            coords[w.key] = cw
            return new THREE.Vector3(offsetX + coords.dx, coords.dz, coords.dy)
          }
          const A = toThree(cornerU, cornerV - sv, 0)
          const A2 = toThree(cornerU, cornerV - sv, w.extent)
          const B = toThree(cornerU - su, cornerV, 0)
          const B2 = toThree(cornerU - su, cornerV, w.extent)
          const facetGeometry = new THREE.BufferGeometry()
          const positions = new Float32Array([
            A.x, A.y, A.z, B.x, B.y, B.z, A2.x, A2.y, A2.z,
            B.x, B.y, B.z, B2.x, B2.y, B2.z, A2.x, A2.y, A2.z,
          ])
          facetGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3))
          const mesh = new THREE.Mesh(facetGeometry, new THREE.MeshStandardMaterial({ color: '#e34d3a', transparent: true, opacity: 0.35, roughness: 0.5, metalness: 0.1, side: THREE.DoubleSide }))
          mesh.add(new THREE.LineSegments(new THREE.EdgesGeometry(facetGeometry), new THREE.LineBasicMaterial({ color: '#8c2f22', transparent: true, opacity: 0.6 })))
          root.add(mesh)
        })
      container.placements.forEach((placement) => {
        const pallet = palletMap.get(placement.box_id)
        if (pallet) {
          // 托盘单元：琥珀色半透明填充 + 虚线边框；激活后外壳更透，便于查看内部
          const active = activePallet?.pallet.pallet_id === placement.box_id
          const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: '#ffb000', transparent: true, opacity: active ? 0.12 : 0.32, roughness: 0.5, metalness: 0.1 }))
          const edges = new THREE.LineSegments(edgeGeometry, new THREE.LineDashedMaterial({ color: '#e08a00', transparent: true, opacity: 0.9, dashSize: 6, gapSize: 4 }))
          edges.computeLineDistances()
          mesh.add(edges)
          mesh.scale.set(placement.dx, placement.dz, placement.dy)
          mesh.position.set(offsetX + placement.x + placement.dx / 2, placement.z + placement.dz / 2, placement.y + placement.dy / 2)
          mesh.userData = { placement }
          root.add(mesh)
          rayTargets.push(mesh)
          return
        }
        const key = categoryOf(placement, colorMode)
        if (visibleKeys.length && !visibleKeys.includes(key)) return
        const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: colors.get(key), roughness: 0.72, metalness: 0.06 }))
        mesh.add(new THREE.LineSegments(edgeGeometry, new THREE.LineBasicMaterial({ color: '#17232a', transparent: true, opacity: 0.72 })))
        mesh.scale.set(placement.dx, placement.dz, placement.dy)
        mesh.position.set(offsetX + placement.x + placement.dx / 2, placement.z + placement.dz / 2, placement.y + placement.dy / 2)
        mesh.userData = { placement }
        root.add(mesh)
        rayTargets.push(mesh)
      })
      // 活动托盘内部散件：原位置渲染在托盘内（托盘自身高度之上）
      if (activePallet && activePallet.containerIndex === index) {
        const shell = palletInfo.get(activePallet.pallet.pallet_id)
        if (shell) {
          activePallet.pallet.placements.forEach((inner) => {
            const innerMesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: colors.get(categoryOf(inner, colorMode)) ?? '#5bc0eb', roughness: 0.7, metalness: 0.08 }))
            innerMesh.add(new THREE.LineSegments(edgeGeometry, new THREE.LineBasicMaterial({ color: '#17232a', transparent: true, opacity: 0.8 })))
            innerMesh.scale.set(inner.dx, inner.dz, inner.dy)
            innerMesh.position.set(
              offsetX + shell.placement.x + inner.x + inner.dx / 2,
              shell.placement.z + activePallet.pallet.sz + inner.z + inner.dz / 2,
              shell.placement.y + inner.y + inner.dy / 2,
            )
            innerMesh.userData = { placement: inner }
            root.add(innerMesh)
            rayTargets.push(innerMesh)
          })
        }
      }
      offsetX += dimensions.x + containerGap
    })
    scene.add(root)
    const raycaster = new THREE.Raycaster()
    const pointer = new THREE.Vector2()
    const canvas = runtime.renderer.domElement
    const placementAt = (event: MouseEvent | PointerEvent) => {
      const rect = canvas.getBoundingClientRect()
      pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1
      pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1
      raycaster.setFromCamera(pointer, camera)
      const hit = raycaster.intersectObjects(rayTargets, true)[0]
      return placementFromObject(hit?.object)
    }
    const handlePointerMove = (event: PointerEvent) => {
      const placement = placementAt(event)
      setHoveredPlacement(placement)
      canvas.style.cursor = placement ? 'pointer' : 'default'
    }
    const handlePointerLeave = () => {
      setHoveredPlacement(null)
      canvas.style.cursor = 'default'
    }
    const handleClick = (event: MouseEvent) => {
      const placement = placementAt(event)
      if (!placement) {
        setActivePallet(null)
        return
      }
      const info = palletInfo.get(placement.box_id)
      if (info) {
        // 点击托盘：进入/退出查看内部（同时聚焦其容器）
        setActivePallet((current) => (current?.pallet.pallet_id === placement.box_id
          ? null
          : { pallet: palletMap.get(placement.box_id)!, placement: info.placement, containerIndex: info.containerIndex }))
        setContainerIndex(info.containerIndex)
        return
      }
      // 点击内部散件保持查看；点击其他箱子/空白退出
      const insideActive = activePallet?.pallet.placements.some((pl) => pl.box_id === placement.box_id)
      if (!insideActive) setActivePallet(null)
    }
    canvas.addEventListener('pointermove', handlePointerMove)
    canvas.addEventListener('pointerleave', handlePointerLeave)
    canvas.addEventListener('click', handleClick)
    return () => {
      canvas.removeEventListener('pointermove', handlePointerMove)
      canvas.removeEventListener('pointerleave', handlePointerLeave)
      canvas.removeEventListener('click', handleClick)
      canvas.style.cursor = 'default'
      scene.remove(root)
      edgeGeometry.dispose()
      geometry.dispose()
    }
  }, [visibleEntries, colorMode, visibleKeys, boxTypes, activePallet, palletMap])

  useEffect(() => {
    const runtime = sceneRef.current
    if (!runtime || !visibleEntries.length) return
    const { camera, controls } = runtime
    if (activePallet) {
      // 聚焦活动托盘（containerIndex 已切换到其容器，坐标为容器局部）
      const pl = activePallet.placement
      const center = new THREE.Vector3(pl.x + pl.dx / 2, pl.z + pl.dz / 2, pl.y + pl.dy / 2)
      const distance = Math.max(pl.dx, pl.dy, pl.dz) * 2.2
      camera.position.copy(center).add(new THREE.Vector3(distance, distance * 0.6, distance))
      controls.target.copy(center)
      controls.update()
      return
    }
    const globalMaxSize = containers.reduce((maximum, container) => Math.max(maximum, container.sx, container.sy, container.sz), 1)
    const layoutWidth = containers.reduce((width, container) => width + container.sx, 0) + Math.max(0, containers.length - 1) * containerGap
    const center = selectedContainer && containerIndex >= 0
      ? new THREE.Vector3(selectedContainer.sx / 2, selectedContainer.sz / 2, selectedContainer.sy / 2)
      : new THREE.Vector3(layoutWidth / 2, globalMaxSize / 2, globalMaxSize / 2)
    const distance = Math.max(globalMaxSize, containerIndex < 0 ? layoutWidth : 0) * 1.8
    const positions: Record<ViewName, THREE.Vector3> = {
      default: new THREE.Vector3(distance, distance * 0.8, distance),
      front: new THREE.Vector3(0, distance * 0.15, distance),
      back: new THREE.Vector3(0, distance * 0.15, -distance),
      left: new THREE.Vector3(-distance, distance * 0.15, 0),
      right: new THREE.Vector3(distance, distance * 0.15, 0),
      top: new THREE.Vector3(0, distance, 0),
      bottom: new THREE.Vector3(0, -distance, 0),
    }
    camera.position.copy(center).add(positions[view])
    controls.target.copy(center)
    controls.update()
  }, [view, containerIndex, containers, visibleEntries, selectedContainer, resetToken, activePallet])

  const allCategories = [...new Set(visibleEntries.flatMap(({ container }) => container.placements.map((placement) => categoryOf(placement, colorMode))))]

  return <Paper withBorder mt="lg"><Group p="md" align="end"><Select label="容器" value={String(containerIndex)} onChange={(value) => { setContainerIndex(Number(value)); setActivePallet(null) }} data={[{ value: '-1', label: '全部容器' }, ...containers.map((container, index) => ({ value: String(index), label: `#${index + 1} · ${container.type_id}` }))]} /><Select label="视角" value={view} onChange={(value) => value && setView(value as ViewName)} data={[{ value: 'default', label: '默认' }, { value: 'front', label: '前视图' }, { value: 'back', label: '后视图' }, { value: 'left', label: '左视图' }, { value: 'right', label: '右视图' }, { value: 'top', label: '俯视图' }, { value: 'bottom', label: '仰视图' }]} /><Select label="着色" value={colorMode} onChange={(value) => { if (value) { setColorMode(value as ColorMode); setVisibleKeys([]) } }} data={[{ value: 'type', label: '箱型' }, { value: 'platform', label: '平台' }, { value: 'group', label: '分组' }]} /><Button variant="default" onClick={() => { setView('default'); setVisibleKeys([]); setActivePallet(null); setResetToken((value) => value + 1) }}>重置</Button></Group><Box pos="relative" h={500}><Box ref={mountRef} h="100%" aria-label="三维装箱结果视图" /><Paper pos="absolute" top={12} left={12} p="sm" shadow="sm"><Stack gap={4}><Text size="xs" c="dimmed">{colorMode === 'type' ? '箱型' : colorMode === 'platform' ? '平台' : '分组'}</Text>{allCategories.map((category, index) => <UnstyledButton key={category} onClick={() => setVisibleKeys((current) => current.includes(category) ? current.filter((item) => item !== category) : [...current, category])}><Group gap="xs" opacity={visibleKeys.length === 0 || visibleKeys.includes(category) ? 1 : 0.35}><Box w={8} h={8} bg={palette[index % palette.length]} /><Text size="xs">{category}</Text></Group></UnstyledButton>)}</Stack></Paper>{hoveredPlacement && (() => {
    const placement = hoveredPlacement
    if (!placement) return null
    const pallet = palletMap.get(placement.box_id)
    if (pallet) return <Paper pos="absolute" right={12} bottom={12} p="sm" shadow="sm" w={260}><Text size="sm" fw={700}>{pallet.pallet_id} · 托盘</Text><Text size="xs" c="dimmed">{pallet.type_id} · {pallet.sx} × {pallet.sy} × {pallet.sz}</Text><Text size="xs">货物堆高 {pallet.used_height} · 承重 {pallet.used_weight} / {pallet.payload}</Text><Text size="xs">体积率 {(pallet.volume_rate * 100).toFixed(1)}% · 内部 {pallet.placements.length} 箱</Text><Text size="xs" c="dimmed">点击查看内部细节</Text></Paper>
    const boxType = boxTypes.find((bt) => bt.id === placement.box_type_id)
    return <Paper pos="absolute" right={12} bottom={12} p="sm" shadow="sm" w={260}><Text size="sm" fw={700}>{placement.box_id}</Text><Text size="xs" c="dimmed">{placement.box_type_id} · {placement.orientation}</Text><Text size="xs">位置 {placement.x}, {placement.y}, {placement.z}</Text><Text size="xs">原始 {boxType?.sx ?? '?'} × {boxType?.sy ?? '?'} × {boxType?.sz ?? '?'} · 放置 {placement.dx} × {placement.dy} × {placement.dz}</Text><Text size="xs">{placement.platform || '无平台'} · {placement.group || '无分组'}</Text></Paper>
  })()}{activePallet && <Paper pos="absolute" left={12} bottom={12} p="sm" shadow="sm" w={280}><Text size="sm" fw={700}>{activePallet.pallet.pallet_id} · 托盘内部</Text><Text size="xs" c="dimmed">{activePallet.pallet.type_id} · 尺寸 {activePallet.pallet.sx} × {activePallet.pallet.sy} × {activePallet.pallet.sz} · 堆高 {activePallet.pallet.used_height}</Text><Text size="xs">承重 {activePallet.pallet.used_weight} / {activePallet.pallet.payload} · 体积率 {(activePallet.pallet.volume_rate * 100).toFixed(1)}%</Text><Text size="xs">内部 {activePallet.pallet.placements.length} 箱 · 点击托盘外壳退出查看</Text></Paper>}</Box></Paper>
}

export default Viewer3D