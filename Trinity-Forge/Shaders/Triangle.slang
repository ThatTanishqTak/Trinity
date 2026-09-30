struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Color : COLOR0;
};

static const float2 s_Positions[3] = { float2(0.0, -0.5), float2(0.5, 0.5), float2(-0.5, 0.5) };
static const float3 s_Colors[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

[shader("vertex")]
VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
	VertexOutput l_Output;
	l_Output.Position = float4(s_Positions[vertexId], 0.0, 1.0);
	l_Output.Color = s_Colors[vertexId];

	return l_Output;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target
{
	return float4(input.Color, 1.0);
}