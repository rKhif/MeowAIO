#pragma once
#include "SDK.h"
#include "Menu.h"
#include "BaseSpell.h"
#include "OrbwalkerData.h"
#include "TargetSelector.h"
#include "Prediction.h"
#include "DamageLib.h"
#include "Graphics.h"
#include "ChampionShared.h"
#include "NavigationMesh.h"
#include <algorithm>
#include <unordered_map>

#include "DelayedActions.h"

namespace Features::Champions::Hwei
{
	template <typename T>
	class FixedSizeVector
	{
	public:
		void Push( const T& Value )
		{
			if ( Data.size() == MaxSize ) { Data.pop_front(); }
			Data.emplace_back( Value );
		}

		std::deque<T> GetVector()
		{
			return Data;
		}

		size_t Size()
		{
			return Data.size();
		}

	protected:
		std::deque<T> Data{};
		static constexpr size_t MaxSize = 10;
	};

	enum class HweiSpell : uint8_t
	{
		QQ,
		QW,
		QE,
		WQ,
		WW,
		WE,
		EQ,
		EW,
		EE,
		R
	};

	enum class HweiSubject : uint8_t
	{
		Subjectless,
		Disaster,
		Serenity,
		Torment
	};

	enum class HweiPriorities : uint8_t
	{
		Zero,
		VeryLow,
		Low,
		Medium,
		High,
		VeryHigh,
		TrueOverride
	};

	enum class HweiCastType : uint8_t
	{
		None,
		CastPosition,
		SinglePred,
		MultiPred,
		Target
	};

	struct HweiQE
	{
		bool IsActive = false;
		Vector3f StartPos = Vector3f::Zero;
		Vector3f EndPos = Vector3f::Zero;
		float CreateTime = 0.f;
	};

	inline HweiQE Tracked = {};

	struct HweiEE
	{
		bool IsActive = false;
		Vector3f CastPosition = Vector3f::Zero;
		AIBaseClient* Target = nullptr;
		float CastTime = 0.f;
		uint8_t SetupAbility = 0;
		uint8_t CastAttempts = 0;
	};

	inline HweiEE CrushingMaw = {};

	inline std::unordered_map<HweiSubject, std::string> HweiSubjectToString = { { HweiSubject::Disaster, xorstr_( "Disaster" ) }, { HweiSubject::Serenity, xorstr_( "Serenity" ) }, { HweiSubject::Torment, xorstr_( "Torment" ) } };
	inline std::unordered_map<Spell::kSpellSlot, std::string> SpellSlotToString = { { Spell::SpellSlot_Q, xorstr_( "Q" ) }, { Spell::SpellSlot_W, xorstr_( "W" ) }, { Spell::SpellSlot_E, xorstr_( "E" ) }, { Spell::SpellSlot_R, xorstr_( "R" ) } };
	inline std::vector<std::string> HweiStringPriorities = { xorstr_( "Zero" ), xorstr_( "Very Low" ), xorstr_( "Low" ), xorstr_( "Medium" ), xorstr_( "High" ), xorstr_( "Very High" ), xorstr_( "Absolute" ) };

	class HweiCastInfo
	{
	public:
		HweiPriorities Priority = HweiPriorities::Zero;
		HweiSubject Subject = HweiSubject::Subjectless;
		Spell::kSpellSlot Slot = Spell::SpellSlot_Q;

		Prediction::PredictionResult PredResult{ .Hitchance = Prediction::Impossible };
		Prediction::MultiTargetPredictionResult MultiPredResult{ .CastPosition = Vector3f::Zero };
		AIBaseClient* Target = nullptr;
		Vector3f CastOverride = Vector3f::Zero;

		HweiCastType CastType = HweiCastType::None;
		bool Process = true;

		float DamageIfApplicable = 0.f;
		float CCTimeIfApplicable = 0.f;

		float PossibleWaitTimeOnCC = 0.f;
	};

	inline MenuSDK::CheckBox* DebugMode = nullptr;

	namespace Disaster
	{
		inline MenuSDK::CheckBox* ComboQ = nullptr;
		inline MenuSDK::CheckBox* CollisionQ = nullptr;
		inline MenuSDK::CheckBox* HarassQ = nullptr;
		inline MenuSDK::List* PriorityQ = nullptr;

		inline MenuSDK::CheckBox* ComboW = nullptr;
		inline MenuSDK::KeyBind* SemiW = nullptr;
		inline MenuSDK::List* SemiWMode = nullptr;
		inline MenuSDK::CheckBox* AutoRQW = nullptr; // Auto Swap to Q if W is guaranteed
		inline MenuSDK::List* PriorityW = nullptr;

		inline MenuSDK::CheckBox* ComboE = nullptr;
		inline MenuSDK::MultiList* UsageE = nullptr;
		inline MenuSDK::Slider* LaneClearE = nullptr;
		inline MenuSDK::List* PriorityE = nullptr;

		float GetDamageQ( AIBaseClient* Client );
		float GetDamageW( AIBaseClient* Client, float Override = -1.f );

		void QueryQ( bool IsCombo = true );
		void QueryW( bool IsCombo = true );
		void QueryE();
		void QueryLaneClearE();
	}

	namespace Serenity
	{
		inline MenuSDK::CheckBox* AllowQ;
		inline MenuSDK::CheckBox* OnGapCloseQ;

		inline MenuSDK::CheckBox* AllowW;
		inline MenuSDK::MultiList* UsageWW;
		inline MenuSDK::MultiList* TargetWW;

		inline MenuSDK::CheckBox* AllowE;
		inline MenuSDK::MultiList* AutoEMode;
		inline MenuSDK::MultiList* AutoESpells;

		float GetDamageE( AIBaseClient* Client );

		void QueryQ();
		void QueryW();
		void QueryE();
		void OnHweiSpellCast( AttackableUnit* Target, HweiSubject Subject, Spell::kSpellSlot Slot );
		void OnNewPath( AIBaseClient* Client, std::vector<Vector3f>* Nodes, bool IsDash, float DashSpeed );
	}

	namespace Torment
	{
		inline MenuSDK::CheckBox* ComboQ;
		inline MenuSDK::CheckBox* SpamQ;
		inline MenuSDK::List* PriorityQ = nullptr;
		inline MenuSDK::CheckBox* ContinueCCQ = nullptr;
		inline MenuSDK::CheckBox* AntiGapCloseQ = nullptr;

		inline MenuSDK::CheckBox* ComboW;
		inline MenuSDK::List* PriorityW = nullptr;
		inline MenuSDK::CheckBox* ContinueCCW = nullptr;
		inline MenuSDK::CheckBox* NonGuaranteedW = nullptr;
		inline MenuSDK::CheckBox* AntiGapCloseW = nullptr;

		inline MenuSDK::CheckBox* ComboE;
		inline MenuSDK::List* PriorityE = nullptr;
		inline MenuSDK::CheckBox* AOE_E = nullptr;
		inline MenuSDK::List* AllowSetupEQ = nullptr;
		inline MenuSDK::CheckBox* AllowEER = nullptr;

		bool IsInCross( Vector3f Origin, Vector3f Position );
		void InitializeEW( MenuSDK::Menu* Parent );
		void QueryQ();
		void QueryW();
		void QueryE();
		float GetDamage( AIBaseClient* Client );
		void OnNewPath( AIBaseClient* Client, std::vector<Vector3f>* Nodes, bool IsDash, float DashSpeed );
	}

	namespace SpiralingDespair
	{
		inline MenuSDK::CheckBox* ComboR = nullptr;
		inline MenuSDK::List* HitchanceR = nullptr;
		inline MenuSDK::KeyBind* SemiR = nullptr;
		inline MenuSDK::List* HitchanceSemiR = nullptr;
		inline std::unordered_map<uint32_t, MenuSDK::CheckBox*> Whitelist{};
	}

	inline std::unordered_map<uint32_t, HweiSpell> ActiveSpells{};
	inline std::unordered_map<uint32_t, Prediction::PredictionResult> HitByEE{};
	inline Vector3f CastPositionEE;
	inline uint8_t InitiateAbilityOnEE = 0;
	inline AIBaseClient* EEAbilityTarget = nullptr;
	inline bool EEWasCasted = false;
	inline float EECastTime = 0.f;

	inline float LastSpecialEW = 0.f;

	inline TargetSelector::Instance* TS;

	inline HweiCastInfo BestToCast{};
	inline BaseSpell QQ, QW, QE, WQ, WW, WE, EQ, EW, EE, R;
	inline BaseSpell FakeEW2;
	inline float CastEndTime = 0.f;

	inline bool WasFirstCastDone = false;

	inline auto EnemyCheck = [] ( AIBaseClient* Target ) { return IsValidDamageTarget( Target ); };
	inline auto SafetyCheck = [] ( AIBaseClient* Target, Prediction::PredictionStatus Status, Prediction::PredictionResult* Result ) { return PredictionSuccess( Status ); };

	inline Vector3f LocalPosition = Vector3f::Zero;

	inline std::unordered_set<AIBaseClient*> Hit{};
	inline Vector3f CastPos = Vector3f::Zero;

	inline std::unordered_map<HweiSubject, std::unordered_map<Spell::kSpellSlot, BaseSpell>> Resolver = {
		{ HweiSubject::Disaster, { { Spell::SpellSlot_Q, QQ }, { Spell::SpellSlot_W, QW }, { Spell::SpellSlot_E, QE } } },
		{ HweiSubject::Serenity, { { Spell::SpellSlot_Q, WQ }, { Spell::SpellSlot_W, WW }, { Spell::SpellSlot_E, WE } } },
		{ HweiSubject::Torment, { { Spell::SpellSlot_Q, EQ }, { Spell::SpellSlot_W, EW }, { Spell::SpellSlot_E, EE } } }
	};

	inline float WaitingForCC = 0.f;
	inline float WaitingForCCTime = 0.f;

	inline uint32_t LevelR = 0;
	inline float EndTimeR = 0.f;

	inline uint32_t SubjectLevel[ 3 ] = { 0, 0, 0 };
	inline float SubjectCooldownEndTime[ 3 ] = { 0.f, 0.f, 0.f };
	inline bool IsLearned[ 4 ] = { false, false, false, false };
	inline std::unordered_map<HweiSubject, float> BlockUntil{ { HweiSubject::Disaster, 0.f }, { HweiSubject::Serenity, 0.f }, { HweiSubject::Torment, 0.f } };
	inline float BlockRUntil = 0.f;
	inline FixedSizeVector<std::string> HweiLogs;

	inline float LastMaxWidth = 0.f;

	inline float LastSubjectLessExitAttempt = 0.f;
	inline float LastCastAttempt = 0.f;
	inline std::unordered_map<HweiSubject, void*> SubjectTextures;
	inline std::unordered_map<HweiSpell, void*> SpellTextures;

	inline DelayedActions HweiActions;
	inline bool WaitingOnAction = true;

	void GoSubjectLess();
	float HealthAfterFullCombo( AIBaseClient* Target, bool IncludeQ = false, bool IncludeW = false, bool IncludeE = false, bool IncludeR = false, float AAs = 3.f );
	float GetDamageP( AIBaseClient* Client );
	float GetDamageR( AIBaseClient* Client );
	bool IsSpellActive( HweiSpell Spell );
	Vector3f RotateRad( Vector3f Vector, float Phi );
	float RadDifference( Vector3f a, Vector3f b );
	bool IsSubjectless();
	void HweiPrint( const char* Format, ... );
	HweiPriorities GetPriority( MenuSDK::List* Component );
	float GetCost( HweiSubject Subject );
	bool IsReady( HweiSubject Subject );
	void Initialize();
}
