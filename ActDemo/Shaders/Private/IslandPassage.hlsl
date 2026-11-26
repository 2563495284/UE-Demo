// Body of M_IslandPassage's UI custom expression. ActPartyFeature embeds it in the cooked material.
// Progress is the local transition clock. Loading delays hold a solid black frame at its endpoint.
float cover = saturate(Progress);
float2 p = (UV - 0.5) * float2(Aspect, 1.0);
float radius = length(p) / (0.5 * length(float2(Aspect, 1.0)));
float angle = atan2(p.y, p.x + 0.00001);
float spin = lerp(1.0, -1.0, Returning);
float travel = spin * cover * Motion;

// Warp two continuous currents together to form curling ink fronts without noise textures.
float swirl = angle + spin * radius * 3.7 + travel * 1.9;
float bend = sin(3.0 * swirl - 2.8 * radius + travel);
float eddy = sin(5.0 * swirl + 2.3 * bend + 6.0 * radius - travel * 1.4);
float current = lerp(0.35, 1.0, Motion) * smoothstep(0.025, 0.32, radius);
float field = radius + current * (0.105 * bend + 0.070 * eddy);
float front = lerp(1.24, -0.23, smoothstep(0.0, 1.0, cover));
float distanceToInk = field - front;
float opacity = smoothstep(-0.09, 0.065, distanceToInk) * smoothstep(0.0, 0.10, cover);
float rim = 1.0 - smoothstep(0.012, 0.115, abs(distanceToInk - 0.01));
float vein = 0.5 + 0.5 * sin(48.0 * distanceToInk + 2.7 * eddy);
float3 deep = lerp(float3(0.008, 0.030, 0.064), float3(0.035, 0.018, 0.056), Returning);
float3 ink = lerp(float3(0.038, 0.20, 0.24), float3(0.20, 0.085, 0.047), Returning);
float3 foam = lerp(float3(0.30, 0.72, 0.66), float3(0.86, 0.47, 0.18), Returning);
float3 color = lerp(deep, ink, 0.20 + 0.33 * vein);
color = lerp(color, foam, rim * (0.54 + 0.44 * vein));
color *= 1.0 - smoothstep(0.68, 0.99, cover);
return float4(color, lerp(opacity, 1.0, smoothstep(0.86, 1.0, cover)));
