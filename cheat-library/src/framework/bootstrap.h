#pragma once

// Framework host bootstrap (P2 5.4). Replaces the hardcoded startup that used to
// live inline in Run(): read framework.manifest.json, apply the enabled
// protections, create the engine adapter named by the manifest, bring the runtime
// up, pick a render backend, then hand off to cheat::Init() to assemble features.
//
// This is per-product host glue: this build ships the Unity(IL2CPP) adapter and
// the Genshin protections, and the manifest describes this single target (release
// form = one host product per target, no multi-target switching).

namespace framework
{
	// Runs the full startup sequence. Returns false (with a logged reason) if the
	// manifest is missing/invalid or the adapter id is unknown -- Run() aborts
	// rather than crash silently. Blocks until the runtime is initialized, same as
	// the old inline flow.
	bool Bootstrap();
}
