import { schemeTableau10 } from 'd3-scale-chromatic'
import { useEffect, useMemo, useRef, useState } from 'react'
import { Box, Button, Group, Paper, Select, Stack, Text, UnstyledButton } from '@mantine/core'
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import type { ContainerResult, Placement } from './api'

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

function Viewer3D({ containers, boxTypes }: {
  containers: ContainerResult[]
  boxTypes: Array<{ id: string; sx: number; sy: number; sz: number }>
}) {
  const mountRef = useRef<HTMLDivElement>(null)
  const sceneRef = useRef<{ scene: THREE.Scene; camera: THREE.PerspectiveCamera; controls: OrbitControls; renderer: THREE.WebGLRenderer } | null>(null)
  const [containerIndex, setContainerIndex] = useState(-1)
  const [colorMode, setColorMode] = useState<ColorMode>('type')
  const [view, setView] = useState<ViewName>('default')
  const [visibleKeys, setVisibleKeys] = useState<string[]>([])
  const [resetToken, setResetToken] = useState(0)
  const [hoveredPlacement, setHoveredPlacement] = useState<Placement | null>(null)

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
    const categoryValues = visibleEntries.flatMap(({ container }) => container.placements.map((placement) => categoryOf(placement, colorMode)))
    const categories = [...new Set(categoryValues)]
    const colors = new Map(categories.map((value, index) => [value, palette[index % palette.length]]))
    const rayTargets: THREE.Object3D[] = []
    const geometry = new THREE.BoxGeometry(1, 1, 1)
    const edgeGeometry = new THREE.EdgesGeometry(geometry)
    let offsetX = 0
    visibleEntries.forEach(({ container }) => {
      const dimensions = { x: container.sx, y: container.sy, z: container.sz }
      const frame = new THREE.LineSegments(
        new THREE.EdgesGeometry(new THREE.BoxGeometry(dimensions.x, dimensions.z, dimensions.y)),
        new THREE.LineBasicMaterial({ color: '#91a5ad', transparent: true, opacity: 0.75 }),
      )
      frame.position.set(offsetX + dimensions.x / 2, dimensions.z / 2, dimensions.y / 2)
      root.add(frame)
        ; (container.obstacles ?? []).forEach((obstacle) => {
          const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color: '#e34d3a', transparent: true, opacity: 0.35, roughness: 0.5, metalness: 0.1 }))
          mesh.add(new THREE.LineSegments(edgeGeometry, new THREE.LineBasicMaterial({ color: '#8c2f22', transparent: true, opacity: 0.6 })))
          mesh.scale.set(obstacle.dx, obstacle.dz, obstacle.dy)
          mesh.position.set(offsetX + obstacle.x + obstacle.dx / 2, obstacle.z + obstacle.dz / 2, obstacle.y + obstacle.dy / 2)
          root.add(mesh)
        })
      container.placements.forEach((placement) => {
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
    canvas.addEventListener('pointermove', handlePointerMove)
    canvas.addEventListener('pointerleave', handlePointerLeave)
    return () => {
      canvas.removeEventListener('pointermove', handlePointerMove)
      canvas.removeEventListener('pointerleave', handlePointerLeave)
      canvas.style.cursor = 'default'
      scene.remove(root)
      edgeGeometry.dispose()
      geometry.dispose()
    }
  }, [visibleEntries, colorMode, visibleKeys, boxTypes])

  useEffect(() => {
    const runtime = sceneRef.current
    if (!runtime || !visibleEntries.length) return
    const { camera, controls } = runtime
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
  }, [view, containerIndex, containers, visibleEntries, selectedContainer, resetToken])

  const allCategories = [...new Set(visibleEntries.flatMap(({ container }) => container.placements.map((placement) => categoryOf(placement, colorMode))))]

  return <Paper withBorder mt="lg"><Group p="md" align="end"><Select label="容器" value={String(containerIndex)} onChange={(value) => setContainerIndex(Number(value))} data={[{ value: '-1', label: '全部容器' }, ...containers.map((container, index) => ({ value: String(index), label: `#${index + 1} · ${container.type_id}` }))]} /><Select label="视角" value={view} onChange={(value) => value && setView(value as ViewName)} data={[{ value: 'default', label: '默认' }, { value: 'front', label: '前视图' }, { value: 'back', label: '后视图' }, { value: 'left', label: '左视图' }, { value: 'right', label: '右视图' }, { value: 'top', label: '俯视图' }, { value: 'bottom', label: '仰视图' }]} /><Select label="着色" value={colorMode} onChange={(value) => { if (value) { setColorMode(value as ColorMode); setVisibleKeys([]) } }} data={[{ value: 'type', label: '箱型' }, { value: 'platform', label: '平台' }, { value: 'group', label: '分组' }]} /><Button variant="default" onClick={() => { setView('default'); setVisibleKeys([]); setResetToken((value) => value + 1) }}>重置</Button></Group><Box pos="relative" h={500}><Box ref={mountRef} h="100%" aria-label="三维装箱结果视图" /><Paper pos="absolute" top={12} left={12} p="sm" shadow="sm"><Stack gap={4}><Text size="xs" c="dimmed">{colorMode === 'type' ? '箱型' : colorMode === 'platform' ? '平台' : '分组'}</Text>{allCategories.map((category, index) => <UnstyledButton key={category} onClick={() => setVisibleKeys((current) => current.includes(category) ? current.filter((item) => item !== category) : [...current, category])}><Group gap="xs" opacity={visibleKeys.length === 0 || visibleKeys.includes(category) ? 1 : 0.35}><Box w={8} h={8} bg={palette[index % palette.length]} /><Text size="xs">{category}</Text></Group></UnstyledButton>)}</Stack></Paper>{hoveredPlacement && (() => {
    const placement = hoveredPlacement
    if (!placement) return null
    const boxType = boxTypes.find((bt) => bt.id === placement.box_type_id)
    return <Paper pos="absolute" right={12} bottom={12} p="sm" shadow="sm" w={260}><Text size="sm" fw={700}>{placement.box_id}</Text><Text size="xs" c="dimmed">{placement.box_type_id} · {placement.orientation}</Text><Text size="xs">位置 {placement.x}, {placement.y}, {placement.z}</Text><Text size="xs">原始 {boxType?.sx ?? '?'} × {boxType?.sy ?? '?'} × {boxType?.sz ?? '?'} · 放置 {placement.dx} × {placement.dy} × {placement.dz}</Text><Text size="xs">{placement.platform || '无平台'} · {placement.group || '无分组'}</Text></Paper>
  })()}</Box></Paper>
}

export default Viewer3D